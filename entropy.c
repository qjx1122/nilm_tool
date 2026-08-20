/**
 * entropy.c - 二次熵编码：LZ4 (via dlopen) + Huffman
 * 条件限制说明在文件末尾
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>

// LZ4 via dlopen
typedef int (*lz4_compress_fn)(const char*, char*, int, int);
typedef int (*lz4_decompress_fn)(const char*, char*, int, int);

static void* lz4_handle = NULL;
static lz4_compress_fn lz4_compress_ptr = NULL;
static lz4_decompress_fn lz4_decompress_ptr = NULL;
static bool lz4_tried = false;

static bool ensure_lz4(){
    if(lz4_tried) return lz4_compress_ptr!=NULL;
    lz4_tried=true;
    lz4_handle = dlopen("liblz4.so.1", RTLD_LAZY);
    if(!lz4_handle) lz4_handle = dlopen("/usr/lib/x86_64-linux-gnu/liblz4.so.1", RTLD_LAZY);
    if(!lz4_handle) lz4_handle = dlopen("liblz4.so", RTLD_LAZY);
    if(!lz4_handle) return false;
    lz4_compress_ptr = (lz4_compress_fn)dlsym(lz4_handle, "LZ4_compress_default");
    lz4_decompress_ptr = (lz4_decompress_fn)dlsym(lz4_handle, "LZ4_decompress_safe");
    return lz4_compress_ptr && lz4_decompress_ptr;
}

// LZ4 压缩：返回压缩后大小，<=0 表示失败或不划算
int lz4_compress_wrapper(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstCap){
    if(!ensure_lz4()) return -1;
    int csize = lz4_compress_ptr((const char*)src, (char*)dst, (int)srcSize, (int)dstCap);
    if(csize<=0) return -1;
    return csize;
}
int lz4_decompress_wrapper(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstCap){
    if(!ensure_lz4()) return -1;
    int dsize = lz4_decompress_ptr((const char*)src, (char*)dst, (int)srcSize, (int)dstCap);
    return dsize;
}

/* ---------- 简单 Huffman 实现 ---------- */
typedef struct HuffNode {
    int freq;
    int symbol; // -1 internal
    struct HuffNode *left, *right;
} HuffNode;

static HuffNode* new_node(int freq, int sym){
    HuffNode* n=(HuffNode*)malloc(sizeof(HuffNode));
    n->freq=freq; n->symbol=sym; n->left=n->right=NULL;
    return n;
}
static void free_tree(HuffNode* r){
    if(!r) return;
    free_tree(r->left); free_tree(r->right); free(r);
}

// 构建霍夫曼树，返回根，codes[256] 存储编码位串，codeLen[256] 长度
static HuffNode* build_huffman_tree(int freq[256], char* codes[256], int codeLen[256]){
    // 简单实现：每次选两个最小频率
    HuffNode* nodes[512];
    int ncount=0;
    for(int i=0;i<256;i++) if(freq[i]>0){
        nodes[ncount++]=new_node(freq[i], i);
        codes[i]=NULL; codeLen[i]=0;
    }
    if(ncount==0) return NULL;
    if(ncount==1){
        // 特殊情况
        codes[nodes[0]->symbol]=strdup("0");
        codeLen[nodes[0]->symbol]=1;
        return nodes[0];
    }
    while(ncount>1){
        // 找两个最小
        int min1=-1,min2=-1;
        for(int i=0;i<ncount;i++){
            if(min1==-1 || nodes[i]->freq < nodes[min1]->freq) {min2=min1; min1=i;}
            else if(min2==-1 || nodes[i]->freq < nodes[min2]->freq) min2=i;
        }
        HuffNode* a=nodes[min1];
        HuffNode* b=nodes[min2];
        HuffNode* parent=new_node(a->freq+b->freq, -1);
        parent->left=a; parent->right=b;
        // 移除 min1,min2，加入 parent
        // 确保 min1<min2 方便删除
        if(min1>min2){int t=min1; min1=min2; min2=t;}
        nodes[min1]=parent;
        nodes[min2]=nodes[ncount-1];
        ncount--;
    }
    // 生成编码
    // DFS
    typedef struct { HuffNode* node; char* code; } StackItem;
    // 使用递归函数生成
    void gen_codes(HuffNode* node, char* prefix){
        if(!node) return;
        if(node->symbol>=0){
            codes[node->symbol]=strdup(prefix);
            codeLen[node->symbol]=strlen(prefix);
            return;
        }
        char leftCode[512], rightCode[512];
        snprintf(leftCode, sizeof(leftCode), "%s0", prefix);
        snprintf(rightCode, sizeof(rightCode), "%s1", prefix);
        gen_codes(node->left, leftCode);
        gen_codes(node->right, rightCode);
    }
    gen_codes(nodes[0], "");
    return nodes[0];
}

// Huffman 压缩：返回压缩后大小（含表头），dst 需足够大 (srcSize*1.1 + 1024)
// 格式：[1B numSymbols][for each symbol present: 1B symbol + 4B freq][4B originalSize][4B compressedBits][bits...]
int huffman_compress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstCap){
    if(srcSize==0) return -1;
    int freq[256]={0};
    for(size_t i=0;i<srcSize;i++) freq[src[i]]++;
    char* codes[256]={0};
    int codeLen[256]={0};
    HuffNode* root=build_huffman_tree(freq, codes, codeLen);
    if(!root) return -1;
    // 计算总位数
    size_t totalBits=0;
    for(int i=0;i<256;i++) if(freq[i]) totalBits+= (size_t)freq[i]*codeLen[i];
    size_t totalBytes = (totalBits+7)/8;
    // 计算表头大小
    int present=0;
    for(int i=0;i<256;i++) if(freq[i]) present++;
    size_t headerSize = 1 + present*(1+4) + 4 + 4; // num, each symbol+freq, origSize, compBits
    if(headerSize+totalBytes > dstCap){
        for(int i=0;i<256;i++) if(codes[i]) free(codes[i]);
        free_tree(root);
        return -1;
    }
    uint8_t* p=dst;
    *p++=(uint8_t)present;
    for(int i=0;i<256;i++) if(freq[i]){
        *p++=(uint8_t)i;
        *((uint32_t*)p)= (uint32_t)freq[i];
        p+=4;
    }
    *((uint32_t*)p)= (uint32_t)srcSize; p+=4;
    *((uint32_t*)p)= (uint32_t)totalBits; p+=4;
    // 编码位流
    size_t bitPos=0;
    memset(p,0,totalBytes);
    for(size_t i=0;i<srcSize;i++){
        int sym=src[i];
        char* code=codes[sym];
        if(!code) continue;
        for(int j=0; code[j]; j++){
            if(code[j]=='1'){
                p[bitPos/8] |= (1 << (7 - (bitPos%8)));
            }
            bitPos++;
        }
    }
    for(int i=0;i<256;i++) if(codes[i]) free(codes[i]);
    free_tree(root);
    return (int)(headerSize+totalBytes);
}

int huffman_decompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstCap){
    if(srcSize<1+4+4) return -1;
    const uint8_t* p=src;
    int present = *p++;
    if(srcSize < 1 + present*5 + 8) return -1;
    int freq[256]={0};
    for(int i=0;i<present;i++){
        int sym = *p++;
        uint32_t f = *((uint32_t*)p); p+=4;
        freq[sym]=f;
    }
    uint32_t origSize = *((uint32_t*)p); p+=4;
    uint32_t compBits = *((uint32_t*)p); p+=4;
    if(origSize > dstCap) return -1;
    char* codes[256]={0};
    int codeLen[256]={0};
    HuffNode* root=build_huffman_tree(freq, codes, codeLen);
    if(!root) return -1;
    // 解码位流
    size_t outPos=0;
    HuffNode* cur=root;
    size_t bitPos=0;
    // 特殊情况：只有一个符号
    if(root->symbol>=0){
        memset(dst, root->symbol, origSize);
        for(int i=0;i<256;i++) if(codes[i]) free(codes[i]);
        free_tree(root);
        return origSize;
    }
    while(outPos<origSize && bitPos<compBits){
        uint8_t byte = p[bitPos/8];
        int bit = (byte >> (7 - (bitPos%8))) & 1;
        bitPos++;
        cur = bit ? cur->right : cur->left;
        if(!cur) break;
        if(cur->symbol>=0){
            dst[outPos++]= (uint8_t)cur->symbol;
            cur=root;
        }
    }
    for(int i=0;i<256;i++) if(codes[i]) free(codes[i]);
    free_tree(root);
    return (int)outPos;
}

/*
 * 使用条件限制说明：
 * 
 * 三相联合编码：
 *   适用条件：三相平衡或近似平衡，零序能量小 (<1% 总能量)，三相尺度相近 (max/min <1.2)，均值接近0。
 *   不适用时：三相不平衡、含大量零序、暂态、单相接地故障等，会回退到独立编码以保证相似度。
 *   实现中检查：zeroEnergy/totalEnergy <0.01 且 scaleRatio <1.2 且 |mean|<阈值，否则不使用联合。
 * 
 * 二次熵编码 (LZ4/Huffman)：
 *   LZ4：适用于包含重复模式、字典可压缩的数据。对已高度量化的高熵数据可能压缩比提升有限，且小文件(<128B)头部开销可能导致膨胀。
 *        条件：仅当二次压缩后尺寸 < 一次压缩尺寸*0.95 (节省>5%) 且原尺寸>64B 时使用。
 *   Huffman：适用于符号分布不均匀的数据。需存储频率表，表头开销约 1+present*5+8 字节，对小文件不划算。
 *           条件：同上，仅当节省>5% 且 present < 200 (符号种类不太多) 时使用。
 *   两者均为无损二次压缩，不影响相似度，只影响压缩比。
 */
