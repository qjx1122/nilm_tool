/**
 * NILM v0.8 - 单周期/多周期 + 三相联合 + 二次熵编码 + 对比文件
 * 输入 data/*.csv -> 输出 out/compressed/, out/reconstructed/, out/comparison/
 */

#define COMPRESSOR_LIB
#include "compress/compress_data.c"
#include "entropy.c"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define OUT_COMPRESSED_DIR "out/compressed"
#define OUT_RECONSTRUCTED_DIR "out/reconstructed"
#define OUT_COMPARISON_DIR "out/comparison"
#define OUT_REPORT_DIR "out"
#define DATA_DIR "data"

typedef struct {
    char** timestamps;
    double *Va, *Vb, *Vc, *Ia, *Ib, *Ic;
    int n;
    int sampleRate;
} WaveData;

typedef struct {
    size_t compBytes;
    double ratio;
    double sim;
    double zeroRatioV;
    double scaleRatioV;
    bool jointUsed;
} SimpleMetrics;

static int ensure_dir(const char* path){
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for(char* p=tmp+1; *p; p++){ if(*p=='/'){ *p='\0'; mkdir(tmp,0755); *p='/'; } }
    mkdir(tmp,0755);
    return 0;
}
static double parse_sec_from_ts(const char* ts){
    const char* ls=strrchr(ts,' ');
    if(!ls) return 0;
    const char* c1=strchr(ls,':');
    if(!c1) return 0;
    const char* c2=strchr(c1+1,':');
    if(!c2) return 0;
    return atof(c2+1);
}
static int estimate_sample_rate(const char* path){
    FILE* f=fopen(path,"r");
    if(!f) return 10000;
    char line[2048];
    if(!fgets(line,sizeof(line),f)){ fclose(f); return 10000; }
    if(!fgets(line,sizeof(line),f)){ fclose(f); return 10000; }
    char ts1[128]={0}; sscanf(line,"%127[^,]",ts1);
    if(!fgets(line,sizeof(line),f)){ fclose(f); return 10000; }
    char ts2[128]={0}; sscanf(line,"%127[^,]",ts2);
    fclose(f);
    double d=parse_sec_from_ts(ts2)-parse_sec_from_ts(ts1);
    if(d<0) d+=60;
    if(d<1e-9||d>1) return 10000;
    int sr=(int)round(1.0/d);
    if(sr<1000||sr>50000) return 10000;
    return sr;
}
static WaveData* read_csv_wave(const char* path){
    FILE* f=fopen(path,"r");
    if(!f) return NULL;
    char line[4096];
    if(!fgets(line,sizeof(line),f)){ fclose(f); return NULL; }
    int idx_ts=-1,idx_ua=-1,idx_ia=-1,idx_ub=-1,idx_ib=-1,idx_uc=-1,idx_ic=-1;
    {
        char hdr[4096]; strncpy(hdr,line,sizeof(hdr));
        char* tok=strtok(hdr,","); int col=0;
        while(tok){
            while(*tok==' '||*tok=='\t') tok++;
            char* e=tok+strlen(tok)-1; while(e>tok && (*e==' '||*e=='\r'||*e=='\n')){*e='\0'; e--;}
            if(strcmp(tok,"timestamp")==0) idx_ts=col;
            else if(strcmp(tok,"UA")==0) idx_ua=col;
            else if(strcmp(tok,"IA")==0) idx_ia=col;
            else if(strcmp(tok,"UB")==0) idx_ub=col;
            else if(strcmp(tok,"IB")==0) idx_ib=col;
            else if(strcmp(tok,"UC")==0) idx_uc=col;
            else if(strcmp(tok,"IC")==0) idx_ic=col;
            tok=strtok(NULL,","); col++;
        }
    }
    if(idx_ts<0||idx_ua<0||idx_ia<0||idx_ub<0||idx_ib<0||idx_uc<0||idx_ic<0){
        if(idx_ts<0) idx_ts=0; if(idx_ua<0) idx_ua=1; if(idx_ia<0) idx_ia=2;
        if(idx_ub<0) idx_ub=3; if(idx_ib<0) idx_ib=4; if(idx_uc<0) idx_uc=5; if(idx_ic<0) idx_ic=6;
    }
    int cap=1024;
    WaveData* wd=calloc(1,sizeof(WaveData));
    wd->timestamps=malloc(cap*sizeof(char*));
    wd->Va=malloc(cap*sizeof(double)); wd->Vb=malloc(cap*sizeof(double)); wd->Vc=malloc(cap*sizeof(double));
    wd->Ia=malloc(cap*sizeof(double)); wd->Ib=malloc(cap*sizeof(double)); wd->Ic=malloc(cap*sizeof(double));
    wd->n=0;
    while(fgets(line,sizeof(line),f)){
        if(strlen(line)<5) continue;
        char* fields[32]; int nf=0; char* p=line; char* s=p;
        while(*p && nf<32){ if(*p==','||*p=='\n'||*p=='\r'){*p='\0'; fields[nf++]=s; s=p+1;} p++; }
        if(nf<=idx_ic) continue;
        if(wd->n>=cap){
            cap*=2;
            wd->timestamps=realloc(wd->timestamps,cap*sizeof(char*));
            wd->Va=realloc(wd->Va,cap*sizeof(double)); wd->Vb=realloc(wd->Vb,cap*sizeof(double)); wd->Vc=realloc(wd->Vc,cap*sizeof(double));
            wd->Ia=realloc(wd->Ia,cap*sizeof(double)); wd->Ib=realloc(wd->Ib,cap*sizeof(double)); wd->Ic=realloc(wd->Ic,cap*sizeof(double));
        }
        wd->timestamps[wd->n]=strdup(fields[idx_ts]);
        wd->Va[wd->n]=atof(fields[idx_ua]); wd->Ia[wd->n]=atof(fields[idx_ia]);
        wd->Vb[wd->n]=atof(fields[idx_ub]); wd->Ib[wd->n]=atof(fields[idx_ib]);
        wd->Vc[wd->n]=atof(fields[idx_uc]); wd->Ic[wd->n]=atof(fields[idx_ic]);
        wd->n++;
    }
    fclose(f);
    wd->sampleRate=estimate_sample_rate(path);
    return wd;
}
static void free_wave_data(WaveData* wd){
    if(!wd) return;
    for(int i=0;i<wd->n;i++) free(wd->timestamps[i]);
    free(wd->timestamps); free(wd->Va); free(wd->Vb); free(wd->Vc); free(wd->Ia); free(wd->Ib); free(wd->Ic); free(wd);
}
static uint8_t* read_file_mem(const char* path, size_t* outSize){
    FILE* f=fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t* buf=malloc(sz); if(!buf){ fclose(f); return NULL; }
    fread(buf,1,sz,f); fclose(f); *outSize=sz; return buf;
}

static SimpleMetrics compress_file_mode(WaveData* wd, const char* compPath, const char* reconPath, int mode, bool useZero, bool multiCycle, bool useJoint){
    SimpleMetrics res; memset(&res,0,sizeof(res));
    int ppc=wd->sampleRate/50;
    if(ppc>MAX_POINTS_PER_CYCLE) ppc=MAX_POINTS_PER_CYCLE;
    if(ppc<16) ppc=16;
    int frames=wd->n/ppc;
    size_t origBytes=(size_t)wd->n*6*sizeof(double);

    CompressionConfig config=create_config(wd->sampleRate, mode);
    config.enableZeroOptimization=useZero;
    config.enableFrameDiff=multiCycle;
    config.enableDictEncoding=true;

    FILE* fcomp=fopen(compPath,"wb");
    FILE* frec=fopen(reconPath,"w");
    // comparison file path
    char compaPath[1024]={0};
    {
        const char* slash=strrchr(compPath,'/');
        const char* name=slash?slash+1:compPath;
        char b[512]; strncpy(b,name,sizeof(b)); char* dot=strrchr(b,'.'); if(dot) *dot='\0';
        snprintf(compaPath,sizeof(compaPath),"%s/%s_comparison.csv",OUT_COMPARISON_DIR,b);
    }
    ensure_dir(OUT_COMPARISON_DIR);
    FILE* fcompa=fopen(compaPath,"w");
    if(!fcomp||!frec){ if(fcomp) fclose(fcomp); if(frec) fclose(frec); if(fcompa) fclose(fcompa); return res; }
    fwrite("NILM",1,4,fcomp);
    uint32_t ver=1; fwrite(&ver,4,1,fcomp);
    int32_t sr=wd->sampleRate, ppc_i=ppc, nf=frames, fl=0;
    if(useZero) fl|=1; if(config.enableDictEncoding) fl|=2; if(multiCycle) fl|=4; if(useJoint) fl|=8;
    fwrite(&sr,4,1,fcomp); fwrite(&ppc_i,4,1,fcomp); fwrite(&nf,4,1,fcomp); fwrite(&fl,4,1,fcomp);
    fprintf(frec,"timestamp,UA,IA,UB,IB,UC,IC\n");
    if(fcompa) fprintf(fcompa,"timestamp,UA_orig,UA_recon,UA_err,IA_orig,IA_recon,IA_err,UB_orig,UB_recon,UB_err,IB_orig,IB_recon,IB_err,UC_orig,UC_recon,UC_err,IC_orig,IC_recon,IC_err\n");

    HybridCompressor enc, dec;
    compressor_init(&enc,&config);
    compressor_init(&dec,&config);

    size_t totalComp=0;
    double totalSim=0;
    double zrV=0,srV=0,zrI=0,srI=0;
    bool jointUsedOverall=false;
    bool first=true;

    for(int fr=0; fr<frames; fr++){
        int off=fr*ppc;
        if(first){
            is_balanced_three_phase(wd->Va+off, wd->Vb+off, wd->Vc+off, wd->Ia+off, wd->Ib+off, wd->Ic+off, ppc, &zrV, &srV, &zrI, &srI);
            first=false;
        }
        CompressedData cd;
        bool usedJoint=false;
        if(useJoint){
            cd=compress_three_phase_joint(&enc, wd->Va+off, wd->Vb+off, wd->Vc+off, wd->Ia+off, wd->Ib+off, wd->Ic+off, ppc, &usedJoint);
            if(usedJoint) jointUsedOverall=true;
        }else{
            cd=compress_three_phase(&enc, wd->Va+off, wd->Vb+off, wd->Vc+off, wd->Ia+off, wd->Ib+off, wd->Ic+off, ppc);
        }
        uint32_t sz[6]={(uint32_t)cd.sizeVa,(uint32_t)cd.sizeVb,(uint32_t)cd.sizeVc,(uint32_t)cd.sizeIa,(uint32_t)cd.sizeIb,(uint32_t)cd.sizeIc};
        fwrite(sz,4,6,fcomp);
        if(cd.sizeVa) fwrite(cd.Va,1,cd.sizeVa,fcomp);
        if(cd.sizeVb) fwrite(cd.Vb,1,cd.sizeVb,fcomp);
        if(cd.sizeVc) fwrite(cd.Vc,1,cd.sizeVc,fcomp);
        if(cd.sizeIa) fwrite(cd.Ia,1,cd.sizeIa,fcomp);
        if(cd.sizeIb) fwrite(cd.Ib,1,cd.sizeIb,fcomp);
        if(cd.sizeIc) fwrite(cd.Ic,1,cd.sizeIc,fcomp);
        totalComp+=cd.totalSize+24;

        double *Va_r=malloc(ppc*sizeof(double)),*Vb_r=malloc(ppc*sizeof(double)),*Vc_r=malloc(ppc*sizeof(double));
        double *Ia_r=malloc(ppc*sizeof(double)),*Ib_r=malloc(ppc*sizeof(double)),*Ic_r=malloc(ppc*sizeof(double));
        int outN;
        if(useJoint) decompress_three_phase_joint(&dec,&cd,Va_r,Vb_r,Vc_r,Ia_r,Ib_r,Ic_r,&outN);
        else decompress_three_phase(&dec,&cd,Va_r,Vb_r,Vc_r,Ia_r,Ib_r,Ic_r,&outN);
        double sim=(calculate_similarity(wd->Va+off,Va_r,ppc)+calculate_similarity(wd->Vb+off,Vb_r,ppc)+calculate_similarity(wd->Vc+off,Vc_r,ppc)+calculate_similarity(wd->Ia+off,Ia_r,ppc)+calculate_similarity(wd->Ib+off,Ib_r,ppc)+calculate_similarity(wd->Ic+off,Ic_r,ppc))/6.0;
        totalSim+=sim;
        for(int k=0;k<ppc;k++){
            int idx=off+k;
            if(idx>=wd->n) break;
            fprintf(frec,"%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",wd->timestamps[idx],Va_r[k],Ia_r[k],Vb_r[k],Ib_r[k],Vc_r[k],Ic_r[k]);
            if(fcompa){
                fprintf(fcompa,"%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",
                    wd->timestamps[idx],
                    wd->Va[off+k], Va_r[k], wd->Va[off+k]-Va_r[k],
                    wd->Ia[off+k], Ia_r[k], wd->Ia[off+k]-Ia_r[k],
                    wd->Vb[off+k], Vb_r[k], wd->Vb[off+k]-Vb_r[k],
                    wd->Ib[off+k], Ib_r[k], wd->Ib[off+k]-Ib_r[k],
                    wd->Vc[off+k], Vc_r[k], wd->Vc[off+k]-Vc_r[k],
                    wd->Ic[off+k], Ic_r[k], wd->Ic[off+k]-Ic_r[k]);
            }
        }
        free(Va_r); free(Vb_r); free(Vc_r); free(Ia_r); free(Ib_r); free(Ic_r);
        free_compressed_data(&cd);
    }
    fclose(fcomp); fclose(frec); if(fcompa) fclose(fcompa);
    res.compBytes=totalComp+20;
    res.ratio=(double)origBytes/(res.compBytes?res.compBytes:1);
    res.sim=frames?totalSim/frames:0;
    res.zeroRatioV=zrV; res.scaleRatioV=srV;
    res.jointUsed=jointUsedOverall;
    return res;
}

int main(int argc, char* argv[]){
    int mode=1;
    bool useZero=true;
    const char* singleInput=NULL;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--mode")==0 && i+1<argc) mode=atoi(argv[++i]);
        else if(strcmp(argv[i],"--zero-opt")==0 && i+1<argc) useZero=atoi(argv[++i])!=0;
        else if(strcmp(argv[i],"--input")==0 && i+1<argc) singleInput=argv[++i];
    }
    ensure_dir(OUT_COMPRESSED_DIR);
    ensure_dir(OUT_RECONSTRUCTED_DIR);
    ensure_dir(OUT_COMPARISON_DIR);
    ensure_dir(DATA_DIR);

    char fileList[64][512];
    int fileCount=0;
    if(singleInput){
        strncpy(fileList[0], singleInput, 511);
        fileCount=1;
    }else{
        DIR* d=opendir(DATA_DIR);
        if(!d){ printf("data 无文件\n"); return 0; }
        struct dirent* e;
        while((e=readdir(d))!=NULL && fileCount<64){
            if(e->d_name[0]=='.') continue;
            char* dot=strrchr(e->d_name,'.');
            if(!dot||strcmp(dot,".csv")!=0) continue;
            snprintf(fileList[fileCount], sizeof(fileList[0]), "%s/%s", DATA_DIR, e->d_name);
            fileCount++;
        }
        closedir(d);
    }

    char reportPath[1024]; snprintf(reportPath,sizeof(reportPath),"%s/verification_report_joint_entropy.csv",OUT_REPORT_DIR);
    FILE* frep=fopen(reportPath,"w");
    if(frep) fprintf(frep,"filename,rows,sampleRate,ppc,frames,origBytes,single_ratio,single_sim,single_joint_ratio,single_joint_sim,jointUsedSingle,multi_ratio,multi_sim,multi_joint_ratio,multi_joint_sim,jointUsedMulti,multi_joint_lz4_ratio,multi_joint_huff_ratio,zeroRatioV,scaleRatioV\n");
    char reportTxtPath[1024]; snprintf(reportTxtPath,sizeof(reportTxtPath),"%s/verification_report_joint_entropy.txt",OUT_REPORT_DIR);
    FILE* frepTxt=fopen(reportTxtPath,"w");
    if(frepTxt){
        fprintf(frepTxt,"NILM 单周期/多周期 + 三相联合 + 熵编码 验证报告\n");
        fprintf(frepTxt,"模式 BALANCED 零序启用\n");
        fprintf(frepTxt,"联合条件: 零序<1%% 且 尺度比<1.2，否则回退独立\n");
        fprintf(frepTxt,"熵编码条件: 二次后 < 一次*0.95 且 >64B\n");
        fprintf(frepTxt,"对比文件: out/comparison/*_comparison.csv 包含 原始/复原/误差\n");
        fprintf(frepTxt,"========================================\n\n");
    }

    double totalOrig=0, totalSingle=0, totalSingleJoint=0, totalMulti=0, totalMultiJoint=0;
    double totalSimSingle=0, totalSimSJ=0, totalSimMulti=0, totalSimMJ=0;

    for(int i=0;i<fileCount;i++){
        char* inputPath=fileList[i];
        printf("\n========== 文件 [%d/%d]: %s ==========\n",i+1,fileCount,inputPath);
        WaveData* wd=read_csv_wave(inputPath);
        if(!wd) continue;
        int ppc=wd->sampleRate/50; if(ppc>MAX_POINTS_PER_CYCLE) ppc=MAX_POINTS_PER_CYCLE; if(ppc<16) ppc=16;
        int frames=wd->n/ppc;
        size_t origBytes=(size_t)wd->n*6*sizeof(double);
        char baseName[512]; const char* slash=strrchr(inputPath,'/'); const char* name=slash?slash+1:inputPath; strncpy(baseName,name,sizeof(baseName)); char* ext=strrchr(baseName,'.'); if(ext) *ext='\0';

        char pSingle[1024], rSingle[1024], pSingleJ[1024], rSingleJ[1024], pMulti[1024], rMulti[1024], pMultiJ[1024], rMultiJ[1024];
        snprintf(pSingle,sizeof(pSingle),"%s/%s_single.bin",OUT_COMPRESSED_DIR,baseName);
        snprintf(rSingle,sizeof(rSingle),"%s/%s_single_reconstructed.csv",OUT_RECONSTRUCTED_DIR,baseName);
        snprintf(pSingleJ,sizeof(pSingleJ),"%s/%s_single_joint.bin",OUT_COMPRESSED_DIR,baseName);
        snprintf(rSingleJ,sizeof(rSingleJ),"%s/%s_single_joint_reconstructed.csv",OUT_RECONSTRUCTED_DIR,baseName);
        snprintf(pMulti,sizeof(pMulti),"%s/%s_multi.bin",OUT_COMPRESSED_DIR,baseName);
        snprintf(rMulti,sizeof(rMulti),"%s/%s_multi_reconstructed.csv",OUT_RECONSTRUCTED_DIR,baseName);
        snprintf(pMultiJ,sizeof(pMultiJ),"%s/%s_multi_joint.bin",OUT_COMPRESSED_DIR,baseName);
        snprintf(rMultiJ,sizeof(rMultiJ),"%s/%s_multi_joint_reconstructed.csv",OUT_RECONSTRUCTED_DIR,baseName);

        SimpleMetrics mSingle = compress_file_mode(wd, pSingle, rSingle, mode, useZero, false, false);
        SimpleMetrics mSingleJ = compress_file_mode(wd, pSingleJ, rSingleJ, mode, useZero, false, true);
        SimpleMetrics mMulti = compress_file_mode(wd, pMulti, rMulti, mode, useZero, true, false);
        SimpleMetrics mMultiJ = compress_file_mode(wd, pMultiJ, rMultiJ, mode, useZero, true, true);

        size_t binSize; uint8_t* binData=read_file_mem(pMultiJ, &binSize);
        size_t lz4Size=0, huffSize=0; double lz4Ratio=0, huffRatio=0; bool lz4Used=false, huffUsed=false;
        if(binData){
            size_t cap=binSize*2+1024;
            uint8_t* dst=malloc(cap);
            if(dst){
                int cs=lz4_compress_wrapper(binData, binSize, dst, cap);
                if(cs>0 && (size_t)cs < binSize*0.95 && binSize>64){
                    char lp[1024]; snprintf(lp,sizeof(lp),"%s/%s_multi_joint.bin.lz4",OUT_COMPRESSED_DIR,baseName);
                    FILE* f=fopen(lp,"wb"); if(f){ fwrite(dst,1,cs,f); fclose(f); lz4Size=cs; lz4Ratio=(double)origBytes/cs; lz4Used=true; }
                }
                int hs=huffman_compress(binData, binSize, dst, cap);
                if(hs>0 && (size_t)hs < binSize*0.95 && binSize>64){
                    char hp[1024]; snprintf(hp,sizeof(hp),"%s/%s_multi_joint.bin.huff",OUT_COMPRESSED_DIR,baseName);
                    FILE* f=fopen(hp,"wb"); if(f){ fwrite(dst,1,hs,f); fclose(f); huffSize=hs; huffRatio=(double)origBytes/hs; huffUsed=true; }
                }
                free(dst);
            }
            free(binData);
        }

        printf("单独立 %.2f:1 %.2f%%  单联合 %.2f:1 %.2f%% %s\n", mSingle.ratio, mSingle.sim*100, mSingleJ.ratio, mSingleJ.sim*100, mSingleJ.jointUsed?"[联合]":"[回退]");
        printf("多独立 %.2f:1 %.2f%%  多联合 %.2f:1 %.2f%% %s\n", mMulti.ratio, mMulti.sim*100, mMultiJ.ratio, mMultiJ.sim*100, mMultiJ.jointUsed?"[联合]":"[回退]");
        if(lz4Used) printf("多联合+LZ4 %.2f:1\n", lz4Ratio); else printf("LZ4 未使用\n");
        if(huffUsed) printf("多联合+Huffman %.2f:1\n", huffRatio); else printf("Huffman 未使用\n");
        printf("对比文件已生成: out/comparison/%s_*_comparison.csv\n", baseName);

        if(frep){
            fprintf(frep,"%s,%d,%d,%d,%d,%zu,%.2f,%.6f,%.2f,%.6f,%d,%.2f,%.6f,%.2f,%.6f,%d,%.2f,%.2f,%.4f,%.4f\n",
                baseName, wd->n, wd->sampleRate, ppc, frames, origBytes,
                mSingle.ratio, mSingle.sim, mSingleJ.ratio, mSingleJ.sim, mSingleJ.jointUsed?1:0,
                mMulti.ratio, mMulti.sim, mMultiJ.ratio, mMultiJ.sim, mMultiJ.jointUsed?1:0,
                lz4Used?lz4Ratio:0, huffUsed?huffRatio:0, mSingle.zeroRatioV, mSingle.scaleRatioV);
        }
        if(frepTxt){
            fprintf(frepTxt,"文件: %s (%d 行, %d Hz, %d 点/周期, %d 帧)\n", baseName, wd->n, wd->sampleRate, ppc, frames);
            fprintf(frepTxt,"  平衡检查: 零序比 %.4f%% 尺度比 %.4f %s\n", mSingle.zeroRatioV*100, mSingle.scaleRatioV, (mSingle.zeroRatioV<0.01 && mSingle.scaleRatioV<1.2)?"[适合联合]":"[不适合]");
            fprintf(frepTxt,"  原始: %zu 字节\n", origBytes);
            fprintf(frepTxt,"  单独立: %.2f:1 相似度 %.4f%%\n", mSingle.ratio, mSingle.sim*100);
            fprintf(frepTxt,"  单联合: %.2f:1 相似度 %.4f%% %s\n", mSingleJ.ratio, mSingleJ.sim*100, mSingleJ.jointUsed?"[联合更优]":"[回退]");
            fprintf(frepTxt,"  多独立: %.2f:1 相似度 %.4f%%\n", mMulti.ratio, mMulti.sim*100);
            fprintf(frepTxt,"  多联合: %.2f:1 相似度 %.4f%% %s\n", mMultiJ.ratio, mMultiJ.sim*100, mMultiJ.jointUsed?"[联合更优]":"[回退]");
            if(lz4Used) fprintf(frepTxt,"  多联合+LZ4: %.2f:1\n", lz4Ratio); else fprintf(frepTxt,"  多联合+LZ4: 未使用\n");
            if(huffUsed) fprintf(frepTxt,"  多联合+Huffman: %.2f:1\n", huffRatio); else fprintf(frepTxt,"  多联合+Huffman: 未使用\n");
            fprintf(frepTxt,"  对比文件: out/comparison/%s_*_comparison.csv (原始/复原/误差)\n", baseName);
            fprintf(frepTxt,"\n");
        }

        totalOrig+=origBytes;
        totalSingle+=mSingle.compBytes; totalSingleJoint+=mSingleJ.compBytes;
        totalMulti+=mMulti.compBytes; totalMultiJoint+=mMultiJ.compBytes;
        totalSimSingle+=mSingle.sim; totalSimSJ+=mSingleJ.sim; totalSimMulti+=mMulti.sim; totalSimMJ+=mMultiJ.sim;

        free_wave_data(wd);
    }

    if(fileCount>0){
        printf("\n========================================\n");
        printf("汇总 %d 文件 总原始 %.2f MB\n", fileCount, totalOrig/1024/1024);
        printf("单独立 %.2f:1 单联合 %.2f:1 多独立 %.2f:1 多联合 %.2f:1\n",
            totalOrig/totalSingle, totalOrig/totalSingleJoint, totalOrig/totalMulti, totalOrig/totalMultiJoint);
        if(frepTxt){
            fprintf(frepTxt,"========================================\n");
            fprintf(frepTxt,"汇总 %d 文件 总原始 %.2f MB\n", fileCount, totalOrig/1024/1024);
            fprintf(frepTxt,"单独立 %.2f:1 单联合 %.2f:1 多独立 %.2f:1 多联合 %.2f:1\n",
                totalOrig/totalSingle, totalOrig/totalSingleJoint, totalOrig/totalMulti, totalOrig/totalMultiJoint);
        }
    }
    if(frep) fclose(frep);
    if(frepTxt) fclose(frepTxt);
    return 0;
}
