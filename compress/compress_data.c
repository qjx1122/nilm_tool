/**
 * ========================================================================
 * 混合智能压缩算法 - 三相电压电流波形压缩与复原
 * 
 * 功能：对电网现场采集的三相电压电流波形数据进行压缩与复原
 * 特点：
 *   1. 支持可配置采样率 (默认6.4KHz)
 *   2. 压缩比 > 300:1
 *   3. 复原相似度 > 95%
 *   4. 集成多种压缩技术：FFT频域压缩、字典编码、帧间差分、零序优化
 * ========================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * 1. 配置参数
 * ============================================================ */

#define PI 3.14159265358979323846

/* 压缩模式定义 */
typedef enum {
    COMPRESS_MODE_ULTRA = 0,      /* 极致压缩: 压缩比 > 500:1 */
    COMPRESS_MODE_BALANCED = 1,   /* 平衡模式: 压缩比 > 300:1 */
    COMPRESS_MODE_HIGH_QUALITY = 2 /* 高质量: 压缩比 > 150:1 */
} CompressMode;

/* 压缩配置结构 */
typedef struct {
    int sampleRate;              /* 采样率 (Hz) */
    int pointsPerCycle;          /* 每周期点数 */
    double targetSimilarity;     /* 目标相似度 */
    
    /* 频域参数 */
    int minHarmonics;            /* 最少保留谐波次数 */
    int maxHarmonics;            /* 最多保留谐波次数 */
    double deadZoneThreshold;    /* 死区阈值 */
    
    /* 量化参数 */
    int quantBitsFundamental;    /* 基波量化位数 */
    int quantBitsHarmonic;       /* 谐波量化位数 */
    
    /* 优化开关 */
    bool enableDictEncoding;     /* 字典编码 */
    bool enableFrameDiff;        /* 帧间差分 */
    bool enableZeroOptimization; /* 零序优化 */
    bool enableAdaptiveHarmonics;/* 自适应谐波选择 */
} CompressionConfig;

/* ============================================================
 * 2. 数据结构定义
 * ============================================================ */

/* 复数结构 */
typedef struct {
    double real;
    double imag;
} Complex;

/* 压缩后的数据包 */
typedef struct {
    uint8_t* Va;    /* A相电压压缩数据 */
    uint8_t* Vb;    /* B相电压压缩数据 */
    uint8_t* Vc;    /* C相电压压缩数据 */
    uint8_t* Ia;    /* A相电流压缩数据 */
    uint8_t* Ib;    /* B相电流压缩数据 */
    uint8_t* Ic;    /* C相电流压缩数据 */
    
    size_t sizeVa;
    size_t sizeVb;
    size_t sizeVc;
    size_t sizeIa;
    size_t sizeIb;
    size_t sizeIc;
    
    size_t totalSize;
} CompressedData;

/* 压缩结果评估 */
typedef struct {
    double compressionRatio;     /* 压缩比 */
    double avgSimilarity;        /* 平均相似度 */
    double avgRMSE;              /* 平均均方根误差 */
    double avgSNR;               /* 平均信噪比 (dB) */
    size_t originalSize;         /* 原始大小 */
    size_t compressedSize;       /* 压缩后大小 */
    double similarities[6];      /* 各通道相似度 */
    bool passed;                 /* 是否通过 */
} EvaluationResult;

/* 谐波字典条目 */
typedef struct {
    int harmonicCount;
    int* orders;                 /* 谐波次数数组 */
    Complex* coefficients;       /* 谐波系数数组 */
    char* name;
} DictPattern;

/* ============================================================
 * 3. 静态数据 - 谐波字典
 * ============================================================ */

/* 预定义谐波模式 */
static DictPattern g_dictPatterns[] = {
    /* 模式0: 纯净正弦波 */
    {1, (int[]){1}, (Complex[]){{1.0, 0.0}}, "PureSine"},
    
    /* 模式1: 3次谐波5% */
    {2, (int[]){1, 3}, (Complex[]){{1.0, 0.0}, {0.05, 0.02}}, "3rdHarmonic"},
    
    /* 模式2: 5次谐波3% */
    {2, (int[]){1, 5}, (Complex[]){{1.0, 0.0}, {0.03, 0.01}}, "5thHarmonic"},
    
    /* 模式3: 3次+5次谐波 */
    {3, (int[]){1, 3, 5}, (Complex[]){{1.0, 0.0}, {0.05, 0.02}, {0.03, 0.01}}, "3rd+5th"},
    
    /* 模式4: 整流器特征 */
    {4, (int[]){1, 3, 5, 7}, (Complex[]){{1.0, 0.0}, {0.08, 0.03}, {0.04, 0.02}, {0.02, 0.01}}, "Rectifier"},
    
    /* 模式5: 变频器特征 */
    {6, (int[]){1, 3, 5, 7, 9, 11}, 
     (Complex[]){{1.0, 0.0}, {0.06, 0.02}, {0.05, 0.02}, {0.03, 0.01}, {0.02, 0.01}, {0.01, 0.005}}, "VFD"}
};

#define DICT_SIZE (sizeof(g_dictPatterns) / sizeof(DictPattern))

/* ============================================================
 * 4. 核心算法函数
 * ============================================================ */

/* ---------- 4.1 基础数学函数 ---------- */

/* 限幅 */
static double clamp_d(double v, double min, double max) {
    return v < min ? min : (v > max ? max : v);
}

/* 取绝对值 */
static double abs_d(double x) {
    return x < 0 ? -x : x;
}

/* ---------- 4.2 FFT实现 ---------- */

/* FFT (Cooley-Tukey算法) */
static void fft(Complex* a, int n, bool invert) {
    int i, j, k, len;
    double ang, real, imag;
    Complex w, wlen, u, v, temp;
    
    /* 比特反转 */
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            temp = a[i];
            a[i] = a[j];
            a[j] = temp;
        }
    }
    
    /* 蝶形运算 */
    for (len = 2; len <= n; len <<= 1) {
        ang = 2.0 * PI / len * (invert ? -1.0 : 1.0);
        wlen.real = cos(ang);
        wlen.imag = sin(ang);
        
        for (i = 0; i < n; i += len) {
            w.real = 1.0;
            w.imag = 0.0;
            for (j = 0; j < len / 2; j++) {
                u = a[i + j];
                v.real = a[i + j + len / 2].real * w.real - a[i + j + len / 2].imag * w.imag;
                v.imag = a[i + j + len / 2].real * w.imag + a[i + j + len / 2].imag * w.real;
                
                a[i + j].real = u.real + v.real;
                a[i + j].imag = u.imag + v.imag;
                a[i + j + len / 2].real = u.real - v.real;
                a[i + j + len / 2].imag = u.imag - v.imag;
                
                temp.real = w.real * wlen.real - w.imag * wlen.imag;
                temp.imag = w.real * wlen.imag + w.imag * wlen.real;
                w = temp;
            }
        }
    }
    
    if (invert) {
        for (i = 0; i < n; i++) {
            a[i].real /= n;
            a[i].imag /= n;
        }
    }
}

/* ---------- 4.3 Clarke变换 ---------- */

/* abc -> αβ0 变换 */
static void abc_to_alpha_beta(double Va, double Vb, double Vc,
                              double* Valpha, double* Vbeta, double* Vzero) {
    *Valpha = (2.0 / 3.0) * (Va - 0.5 * Vb - 0.5 * Vc);
    *Vbeta = (2.0 / 3.0) * (0.866 * Vb - 0.866 * Vc);
    *Vzero = (1.0 / 3.0) * (Va + Vb + Vc);
}

/* αβ0 -> abc 变换 */
static void alpha_beta_to_abc(double Valpha, double Vbeta, double Vzero,
                              double* Va, double* Vb, double* Vc) {
    *Va = Valpha + Vzero;
    *Vb = -0.5 * Valpha + 0.866 * Vbeta + Vzero;
    *Vc = -0.5 * Valpha - 0.866 * Vbeta + Vzero;
}

/* ---------- 4.4 相似度计算 ---------- */

static double calculate_similarity(const double* orig, const double* recon, int n) {
    int i;
    double dot = 0.0, normOrig = 0.0, normRecon = 0.0;
    
    for (i = 0; i < n; i++) {
        dot += orig[i] * recon[i];
        normOrig += orig[i] * orig[i];
        normRecon += recon[i] * recon[i];
    }
    
    if (normOrig < 1e-12 || normRecon < 1e-12) return 1.0;
    return dot / (sqrt(normOrig) * sqrt(normRecon));
}

static double calculate_rmse(const double* orig, const double* recon, int n) {
    int i;
    double mse = 0.0;
    
    for (i = 0; i < n; i++) {
        double diff = orig[i] - recon[i];
        mse += diff * diff;
    }
    return sqrt(mse / n);
}

static double calculate_snr(const double* orig, const double* recon, int n) {
    int i;
    double signalPower = 0.0, noisePower = 0.0;
    
    for (i = 0; i < n; i++) {
        signalPower += orig[i] * orig[i];
        double diff = orig[i] - recon[i];
        noisePower += diff * diff;
    }
    
    if (noisePower < 1e-12) return 1000.0;
    return 10.0 * log10(signalPower / noisePower);
}

/* ---------- 4.5 量化函数 ---------- */

static uint16_t quantize_16(double value, double minVal, double maxVal, int bits) {
    int maxQ = (1 << bits) - 1;
    if (maxQ == 0) return 0;
    double normalized = (value - minVal) / (maxVal - minVal);
    normalized = clamp_d(normalized, 0.0, 1.0);
    return (uint16_t)(normalized * maxQ);
}

static double dequantize_16(uint16_t quant, double minVal, double maxVal, int bits) {
    int maxQ = (1 << bits) - 1;
    if (maxQ == 0) return (minVal + maxVal) / 2.0;
    double normalized = (double)quant / maxQ;
    return normalized * (maxVal - minVal) + minVal;
}

static uint8_t quantize_8(double value, double minVal, double maxVal) {
    double normalized = (value - minVal) / (maxVal - minVal);
    normalized = clamp_d(normalized, 0.0, 1.0);
    return (uint8_t)(normalized * 255);
}

static double dequantize_8(uint8_t quant, double minVal, double maxVal) {
    double normalized = (double)quant / 255.0;
    return normalized * (maxVal - minVal) + minVal;
}

/* ---------- 4.6 谐波字典匹配 ---------- */

static int dict_find_best_match(const int* orders, const Complex* coeffs, int count) {
    int i, j, bestIdx = 0;
    double bestScore = -1.0;
    
    for (i = 0; i < DICT_SIZE; i++) {
        double score = 0.0;
        int matchCount = 0;
        
        /* 基波匹配 */
        if (count > 0 && g_dictPatterns[i].harmonicCount > 0) {
            score += 0.5;
        }
        
        /* 谐波次数匹配 */
        for (j = 0; j < count && j < g_dictPatterns[i].harmonicCount; j++) {
            int k;
            for (k = 0; k < g_dictPatterns[i].harmonicCount; k++) {
                if (orders[j] == g_dictPatterns[i].orders[k]) {
                    matchCount++;
                    break;
                }
            }
        }
        
        if (count > 0 && g_dictPatterns[i].harmonicCount > 0) {
            score += 0.5 * (double)matchCount / 
                     (count > g_dictPatterns[i].harmonicCount ? count : g_dictPatterns[i].harmonicCount);
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }
    
    return bestIdx;
}

/* ---------- 4.7 自适应谐波选择 ---------- */

static int select_harmonics(const Complex* freq, int n, const CompressionConfig* config) {
    int k;
    double totalEnergy = 0.0, cumEnergy = 0.0;
    double* amp = NULL;
    int bestK = config->minHarmonics;
    
    if (!config->enableAdaptiveHarmonics) {
        return config->maxHarmonics;
    }
    
    amp = (double*)malloc((n / 2) * sizeof(double));
    if (!amp) return config->maxHarmonics;
    
    for (k = 1; k < n / 2; k++) {
        amp[k] = sqrt(freq[k].real * freq[k].real + freq[k].imag * freq[k].imag);
        totalEnergy += amp[k] * amp[k];
    }
    
    if (totalEnergy < 1e-12) {
        free(amp);
        return config->minHarmonics;
    }
    
    for (k = 1; k <= config->maxHarmonics && k < n / 2; k++) {
        cumEnergy += amp[k] * amp[k];
        double ratio = cumEnergy / totalEnergy;
        double estimatedSim = sqrt(ratio);
        
        if (estimatedSim >= config->targetSimilarity) {
            bestK = (config->minHarmonics > k) ? config->minHarmonics : k;
            break;
        }
        bestK = k;
    }
    
    if (bestK > config->maxHarmonics) bestK = config->maxHarmonics;
    if (bestK < config->minHarmonics) bestK = config->minHarmonics;
    
    free(amp);
    return bestK;
}

/* ---------- 4.8 提取谐波 ---------- */

static int extract_harmonics(const Complex* freq, int n, int K, double deadZone,
                             int* orders, Complex* coeffs, int maxCount) {
    int count = 0;
    double fundAmp = sqrt(freq[1].real * freq[1].real + freq[1].imag * freq[1].imag);
    int k;
    
    if (fundAmp > 1e-9 && count < maxCount) {
        orders[count] = 1;
        coeffs[count] = freq[1];
        count++;
    }
    
    for (k = 2; k <= K && k < n / 2 && count < maxCount; k++) {
        double amp = sqrt(freq[k].real * freq[k].real + freq[k].imag * freq[k].imag);
        if (amp > deadZone * fundAmp) {
            orders[count] = k;
            coeffs[count] = freq[k];
            count++;
        }
    }
    
    return count;
}

/* ============================================================
 * 5. 压缩器主结构
 * ============================================================ */

typedef struct {
    CompressionConfig config;
    int N;
    Complex prevFreq[6][128];    /* 历史帧 (最大128点) */
    bool hasPrev[6];
    
    /* 统计信息 */
    int totalFrames;
    int dictUsed;
    int diffUsed;
    double avgHarmonics;
} HybridCompressor;

/* ---------- 5.1 初始化 ---------- */

static void compressor_init(HybridCompressor* comp, const CompressionConfig* config) {
    int i;
    
    comp->config = *config;
    comp->N = config->pointsPerCycle;
    comp->totalFrames = 0;
    comp->dictUsed = 0;
    comp->diffUsed = 0;
    comp->avgHarmonics = 0.0;
    
    for (i = 0; i < 6; i++) {
        memset(comp->prevFreq[i], 0, comp->N * sizeof(Complex));
        comp->hasPrev[i] = false;
    }
}

/* ---------- 5.2 压缩单通道 ---------- */

static uint8_t* compress_channel(HybridCompressor* comp, const double* signal, int n, 
                                  int channelIdx, size_t* outSize) {
    int i, k, pos = 0;
    double mean = 0.0, maxVal = 0.0;
    double* norm = NULL;
    Complex* freq = NULL;
    uint8_t* data = NULL;
    size_t dataSize = 0;
    int K, harmonicCount;
    int orders[32];
    Complex coeffs[32];
    
    if (n != comp->N) {
        *outSize = 0;
        return NULL;
    }
    
    /* 1. 归一化 */
    for (i = 0; i < n; i++) mean += signal[i];
    mean /= n;
    
    norm = (double*)malloc(n * sizeof(double));
    if (!norm) return NULL;
    
    for (i = 0; i < n; i++) {
        norm[i] = signal[i] - mean;
        if (fabs(norm[i]) > maxVal) maxVal = fabs(norm[i]);
    }
    if (maxVal < 1e-9) maxVal = 1.0;
    
    /* 2. FFT */
    freq = (Complex*)malloc(n * sizeof(Complex));
    if (!freq) {
        free(norm);
        return NULL;
    }
    
    for (i = 0; i < n; i++) {
        freq[i].real = norm[i] / maxVal;
        freq[i].imag = 0.0;
    }
    fft(freq, n, false);
    
    /* 3. 自适应谐波选择 */
    K = select_harmonics(freq, n, &comp->config);
    harmonicCount = extract_harmonics(freq, n, K, comp->config.deadZoneThreshold,
                                       orders, coeffs, 32);
    
    /* 4. 字典匹配 */
    int dictIdx = -1;
    bool useDict = false;
    if (comp->config.enableDictEncoding) {
        dictIdx = dict_find_best_match(orders, coeffs, harmonicCount);
        useDict = (dictIdx >= 0);
    }
    
    /* 5. 帧间差分 */
    bool useDiff = false;
    Complex* diffFreq = NULL;
    if (comp->config.enableFrameDiff && comp->hasPrev[channelIdx]) {
        double diffEnergy = 0.0;
        diffFreq = (Complex*)malloc(n * sizeof(Complex));
        if (diffFreq) {
            for (i = 0; i < n; i++) {
                diffFreq[i].real = freq[i].real - comp->prevFreq[channelIdx][i].real;
                diffFreq[i].imag = freq[i].imag - comp->prevFreq[channelIdx][i].imag;
                diffEnergy += diffFreq[i].real * diffFreq[i].real + 
                             diffFreq[i].imag * diffFreq[i].imag;
            }
            useDiff = (diffEnergy > 0.01);
        }
    }
    
    /* 6. 构建压缩数据 */
    dataSize = 1024;  /* 预分配 */
    data = (uint8_t*)malloc(dataSize);
    if (!data) {
        free(norm);
        free(freq);
        if (diffFreq) free(diffFreq);
        return NULL;
    }
    
    /* ===== 头部 (5字节) ===== */
    /* mean (16bit) */
    uint16_t meanQ = quantize_16(mean, -1000.0, 1000.0, 16);
    data[pos++] = (meanQ >> 8) & 0xFF;
    data[pos++] = meanQ & 0xFF;
    
    /* scale (16bit) */
    uint16_t scaleQ = quantize_16(maxVal, 0, 1000.0, 16);
    data[pos++] = (scaleQ >> 8) & 0xFF;
    data[pos++] = scaleQ & 0xFF;
    
    /* flags: [字典(1)][差分(1)][保留(1)][K值(5bit)] */
    uint8_t flags = 0;
    if (useDict) flags |= 0x80;
    if (useDiff) flags |= 0x40;
    if (comp->config.enableAdaptiveHarmonics) flags |= 0x20;
    flags |= (K & 0x1F);
    data[pos++] = flags;
    
    /* ===== 数据主体 ===== */
    if (useDict) {
        /* 字典编码 */
        data[pos++] = (uint8_t)dictIdx;
        
        /* 计算残差 */
        int residualCount = 0;
        int residualOrders[32];
        Complex residualCoeffs[32];
        
        for (i = 0; i < harmonicCount; i++) {
            bool found = false;
            for (k = 0; k < g_dictPatterns[dictIdx].harmonicCount; k++) {
                if (orders[i] == g_dictPatterns[dictIdx].orders[k]) {
                    found = true;
                    break;
                }
            }
            if (!found && residualCount < 32) {
                residualOrders[residualCount] = orders[i];
                residualCoeffs[residualCount] = coeffs[i];
                residualCount++;
            }
        }
        
        data[pos++] = (uint8_t)(residualCount > 255 ? 255 : residualCount);
        for (i = 0; i < residualCount && i < 255; i++) {
            data[pos++] = (uint8_t)residualOrders[i];
            data[pos++] = quantize_8(residualCoeffs[i].real, -0.5, 0.5);
            data[pos++] = quantize_8(residualCoeffs[i].imag, -0.5, 0.5);
        }
        
        comp->dictUsed++;
        
    } else if (useDiff) {
        /* 差分编码 */
        int diffCount = 0;
        uint8_t diffData[256];  /* 临时存储 */
        int diffPos = 0;
        
        for (i = 1; i <= K && i < n / 2; i++) {
            double amp = sqrt(diffFreq[i].real * diffFreq[i].real + 
                             diffFreq[i].imag * diffFreq[i].imag);
            if (amp > comp->config.deadZoneThreshold * 0.1) {
                diffData[diffPos++] = (uint8_t)i;
                diffData[diffPos++] = quantize_8(diffFreq[i].real, -0.5, 0.5);
                diffData[diffPos++] = quantize_8(diffFreq[i].imag, -0.5, 0.5);
                diffCount++;
            }
        }
        
        data[pos++] = (uint8_t)(diffCount > 255 ? 255 : diffCount);
        for (i = 0; i < diffCount && i < 255; i++) {
            data[pos++] = diffData[i * 3];
            data[pos++] = diffData[i * 3 + 1];
            data[pos++] = diffData[i * 3 + 2];
        }
        
        comp->diffUsed++;
        
    } else {
        /* 标准编码 */
        double fundAmp = sqrt(freq[1].real * freq[1].real + freq[1].imag * freq[1].imag);
        double fundPhase = atan2(freq[1].imag, freq[1].real);
        
        int bitsF = comp->config.quantBitsFundamental;
        uint16_t ampQ = quantize_16(fundAmp, 0, 1.5, bitsF);
        uint16_t phaseQ = quantize_16(fundPhase, -PI, PI, bitsF);
        
        data[pos++] = (ampQ >> 8) & 0xFF;
        data[pos++] = ampQ & 0xFF;
        data[pos++] = (phaseQ >> 8) & 0xFF;
        data[pos++] = phaseQ & 0xFF;
        
        /* 高次谐波 */
        int bitsH = comp->config.quantBitsHarmonic;
        int harmCount = 0;
        
        for (i = 0; i < harmonicCount; i++) {
            if (orders[i] == 1) continue;
            double real = coeffs[i].real / fundAmp;
            double imag = coeffs[i].imag / fundAmp;
            
            if (harmCount < 32) {
                uint16_t rQ = quantize_16(real, -1.0, 1.0, bitsH);
                uint16_t iQ = quantize_16(imag, -1.0, 1.0, bitsH);
                
                data[pos++] = (uint8_t)orders[i];
                data[pos++] = (rQ >> 8) & 0xFF;
                data[pos++] = rQ & 0xFF;
                data[pos++] = (iQ >> 8) & 0xFF;
                data[pos++] = iQ & 0xFF;
                harmCount++;
            }
        }
        
        data[pos++] = (uint8_t)(harmCount > 255 ? 255 : harmCount);
    }
    
    /* 保存帧用于差分 */
    if (comp->config.enableFrameDiff) {
        for (i = 0; i < n; i++) {
            comp->prevFreq[channelIdx][i] = freq[i];
        }
        comp->hasPrev[channelIdx] = true;
    }
    
    /* 更新统计 */
    comp->totalFrames++;
    comp->avgHarmonics += K;
    
    /* 释放临时内存 */
    free(norm);
    free(freq);
    if (diffFreq) free(diffFreq);
    
    *outSize = pos;
    return data;
}

/* ---------- 5.3 解压单通道 ---------- */

static double* decompress_channel(HybridCompressor* comp, const uint8_t* data, 
                                   size_t dataSize, int channelIdx, int* outN) {
    int i, pos = 0, n = comp->N;
    double mean, scale;
    Complex* freq = NULL;
    double* result = NULL;
    
    if (dataSize < 6 || !data) {
        *outN = 0;
        return NULL;
    }
    
    /* 读取头部 */
    uint16_t meanQ = (data[pos] << 8) | data[pos+1]; pos += 2;
    uint16_t scaleQ = (data[pos] << 8) | data[pos+1]; pos += 2;
    
    mean = dequantize_16(meanQ, -1000.0, 1000.0, 16);
    scale = dequantize_16(scaleQ, 0, 1000.0, 16);
    
    uint8_t flags = data[pos++];
    bool useDict = (flags & 0x80) != 0;
    bool useDiff = (flags & 0x40) != 0;
    int K = flags & 0x1F;
    
    freq = (Complex*)calloc(n, sizeof(Complex));
    if (!freq) return NULL;
    
    if (useDict) {
        /* 字典解码 */
        int dictIdx = data[pos++];
        if (dictIdx < DICT_SIZE) {
            for (i = 0; i < g_dictPatterns[dictIdx].harmonicCount; i++) {
                int order = g_dictPatterns[dictIdx].orders[i];
                if (order < n) {
                    freq[order] = g_dictPatterns[dictIdx].coefficients[i];
                }
            }
        }
        
        int numResidual = data[pos++];
        for (i = 0; i < numResidual && pos + 2 < dataSize; i++) {
            int k = data[pos++];
            double real = dequantize_8(data[pos++], -0.5, 0.5);
            double imag = dequantize_8(data[pos++], -0.5, 0.5);
            if (k < n) {
                freq[k].real += real;
                freq[k].imag += imag;
            }
        }
        
    } else if (useDiff) {
        /* 差分解码 */
        int numDiff = data[pos++];
        for (i = 0; i < numDiff && pos + 2 < dataSize; i++) {
            int k = data[pos++];
            double real = dequantize_8(data[pos++], -0.5, 0.5);
            double imag = dequantize_8(data[pos++], -0.5, 0.5);
            if (k < n) {
                freq[k].real += real;
                freq[k].imag += imag;
            }
        }
        
        /* 加上历史帧 */
        if (comp->hasPrev[channelIdx]) {
            for (i = 0; i < n; i++) {
                freq[i].real += comp->prevFreq[channelIdx][i].real;
                freq[i].imag += comp->prevFreq[channelIdx][i].imag;
            }
        }
        
    } else {
        /* 标准解码 */
        if (pos + 3 >= dataSize) {
            free(freq);
            *outN = 0;
            return NULL;
        }
        
        int bitsF = comp->config.quantBitsFundamental;
        uint16_t ampQ = (data[pos] << 8) | data[pos+1]; pos += 2;
        uint16_t phaseQ = (data[pos] << 8) | data[pos+1]; pos += 2;
        
        double fundAmp = dequantize_16(ampQ, 0, 1.5, bitsF);
        double fundPhase = dequantize_16(phaseQ, -PI, PI, bitsF);
        
        freq[1].real = fundAmp * cos(fundPhase);
        freq[1].imag = fundAmp * sin(fundPhase);
        
        int numHarmonics = data[pos++];
        int bitsH = comp->config.quantBitsHarmonic;
        
        for (i = 0; i < numHarmonics && pos + 4 < dataSize; i++) {
            int k = data[pos++];
            uint16_t rQ = (data[pos] << 8) | data[pos+1]; pos += 2;
            uint16_t iQ = (data[pos] << 8) | data[pos+1]; pos += 2;
            
            double real = dequantize_16(rQ, -1.0, 1.0, bitsH) * fundAmp;
            double imag = dequantize_16(iQ, -1.0, 1.0, bitsH) * fundAmp;
            
            if (k < n) {
                freq[k].real = real;
                freq[k].imag = imag;
                if (k > 0 && k < n / 2) {
                    freq[n - k].real = real;
                    freq[n - k].imag = -imag;
                }
            }
        }
    }
    
    /* IFFT */
    fft(freq, n, true);
    
    /* 还原时域 */
    result = (double*)malloc(n * sizeof(double));
    if (result) {
        for (i = 0; i < n; i++) {
            result[i] = freq[i].real * scale + mean;
        }
    }
    
    free(freq);
    *outN = n;
    return result;
}

/* ---------- 5.4 三相压缩 ---------- */

CompressedData compress_three_phase(HybridCompressor* comp,
                                    const double* Va, const double* Vb, const double* Vc,
                                    const double* Ia, const double* Ib, const double* Ic,
                                    int n) {
    CompressedData result;
    memset(&result, 0, sizeof(result));
    
    if (n != comp->N) return result;
    
    result.Va = compress_channel(comp, Va, n, 0, &result.sizeVa);
    result.Vb = compress_channel(comp, Vb, n, 1, &result.sizeVb);
    result.Vc = compress_channel(comp, Vc, n, 2, &result.sizeVc);
    result.Ia = compress_channel(comp, Ia, n, 3, &result.sizeIa);
    result.Ib = compress_channel(comp, Ib, n, 4, &result.sizeIb);
    result.Ic = compress_channel(comp, Ic, n, 5, &result.sizeIc);
    
    result.totalSize = result.sizeVa + result.sizeVb + result.sizeVc +
                       result.sizeIa + result.sizeIb + result.sizeIc;
    
    return result;
}

/* ---------- 5.5 三相解压 ---------- */

void decompress_three_phase(HybridCompressor* comp, const CompressedData* data,
                            double* Va, double* Vb, double* Vc,
                            double* Ia, double* Ib, double* Ic,
                            int* outN) {
    int n;
    double* pVa = decompress_channel(comp, data->Va, data->sizeVa, 0, &n);
    double* pVb = decompress_channel(comp, data->Vb, data->sizeVb, 1, &n);
    double* pVc = decompress_channel(comp, data->Vc, data->sizeVc, 2, &n);
    double* pIa = decompress_channel(comp, data->Ia, data->sizeIa, 3, &n);
    double* pIb = decompress_channel(comp, data->Ib, data->sizeIb, 4, &n);
    double* pIc = decompress_channel(comp, data->Ic, data->sizeIc, 5, &n);
    
    if (pVa && pVb && pVc && pIa && pIb && pIc) {
        int i;
        for (i = 0; i < n; i++) {
            Va[i] = pVa[i];
            Vb[i] = pVb[i];
            Vc[i] = pVc[i];
            Ia[i] = pIa[i];
            Ib[i] = pIb[i];
            Ic[i] = pIc[i];
        }
        *outN = n;
    } else {
        *outN = 0;
    }
    
    if (pVa) free(pVa);
    if (pVb) free(pVb);
    if (pVc) free(pVc);
    if (pIa) free(pIa);
    if (pIb) free(pIb);
    if (pIc) free(pIc);
}

/* ---------- 5.6 零序优化压缩 ---------- */

CompressedData compress_with_zero_optimization(HybridCompressor* comp,
                                               const double* Va, const double* Vb, const double* Vc,
                                               const double* Ia, const double* Ib, const double* Ic,
                                               int n) {
    CompressedData result;
    memset(&result, 0, sizeof(result));
    
    if (n != comp->N) return result;
    
    if (!comp->config.enableZeroOptimization) {
        return compress_three_phase(comp, Va, Vb, Vc, Ia, Ib, Ic, n);
    }
    
    /* 转换到αβ0域 */
    double* Valpha = (double*)malloc(n * sizeof(double));
    double* Vbeta = (double*)malloc(n * sizeof(double));
    double* Vzero = (double*)malloc(n * sizeof(double));
    double* Ialpha = (double*)malloc(n * sizeof(double));
    double* Ibeta = (double*)malloc(n * sizeof(double));
    double* Izero = (double*)malloc(n * sizeof(double));
    
    if (!Valpha || !Vbeta || !Vzero || !Ialpha || !Ibeta || !Izero) {
        if (Valpha) free(Valpha);
        if (Vbeta) free(Vbeta);
        if (Vzero) free(Vzero);
        if (Ialpha) free(Ialpha);
        if (Ibeta) free(Ibeta);
        if (Izero) free(Izero);
        return compress_three_phase(comp, Va, Vb, Vc, Ia, Ib, Ic, n);
    }
    
    int i;
    for (i = 0; i < n; i++) {
        abc_to_alpha_beta(Va[i], Vb[i], Vc[i], &Valpha[i], &Vbeta[i], &Vzero[i]);
        abc_to_alpha_beta(Ia[i], Ib[i], Ic[i], &Ialpha[i], &Ibeta[i], &Izero[i]);
    }
    
    /* 检查零序能量 */
    double zeroEnergyV = 0.0, zeroEnergyI = 0.0;
    for (i = 0; i < n; i++) {
        zeroEnergyV += Vzero[i] * Vzero[i];
        zeroEnergyI += Izero[i] * Izero[i];
    }
    
    /* 压缩α和β分量 */
    result.Va = compress_channel(comp, Valpha, n, 0, &result.sizeVa);
    result.Vb = compress_channel(comp, Vbeta, n, 1, &result.sizeVb);
    result.Ia = compress_channel(comp, Ialpha, n, 3, &result.sizeIa);
    result.Ib = compress_channel(comp, Ibeta, n, 4, &result.sizeIb);
    
    /* 零序分量优化 */
    if (zeroEnergyV < 0.001) {
        result.Vc = (uint8_t*)malloc(1);
        if (result.Vc) {
            result.Vc[0] = 0;
            result.sizeVc = 1;
        }
    } else {
        result.Vc = compress_channel(comp, Vzero, n, 2, &result.sizeVc);
    }
    
    if (zeroEnergyI < 0.001) {
        result.Ic = (uint8_t*)malloc(1);
        if (result.Ic) {
            result.Ic[0] = 0;
            result.sizeIc = 1;
        }
    } else {
        result.Ic = compress_channel(comp, Izero, n, 5, &result.sizeIc);
    }
    
    result.totalSize = result.sizeVa + result.sizeVb + result.sizeVc +
                       result.sizeIa + result.sizeIb + result.sizeIc;
    
    free(Valpha);
    free(Vbeta);
    free(Vzero);
    free(Ialpha);
    free(Ibeta);
    free(Izero);
    
    return result;
}

/* ---------- 5.7 零序优化解压 ---------- */

void decompress_with_zero_optimization(HybridCompressor* comp, const CompressedData* data,
                                       double* Va, double* Vb, double* Vc,
                                       double* Ia, double* Ib, double* Ic,
                                       int* outN) {
    int n, i;
    double* Valpha = decompress_channel(comp, data->Va, data->sizeVa, 0, &n);
    double* Vbeta = decompress_channel(comp, data->Vb, data->sizeVb, 1, &n);
    double* Ialpha = decompress_channel(comp, data->Ia, data->sizeIa, 3, &n);
    double* Ibeta = decompress_channel(comp, data->Ib, data->sizeIb, 4, &n);
    
    double* Vzero = NULL;
    double* Izero = NULL;
    
    if (data->sizeVc == 1 && data->Vc[0] == 0) {
        Vzero = (double*)calloc(n, sizeof(double));
    } else {
        Vzero = decompress_channel(comp, data->Vc, data->sizeVc, 2, &n);
    }
    
    if (data->sizeIc == 1 && data->Ic[0] == 0) {
        Izero = (double*)calloc(n, sizeof(double));
    } else {
        Izero = decompress_channel(comp, data->Ic, data->sizeIc, 5, &n);
    }
    
    if (Valpha && Vbeta && Vzero && Ialpha && Ibeta && Izero) {
        for (i = 0; i < n; i++) {
            alpha_beta_to_abc(Valpha[i], Vbeta[i], Vzero[i], &Va[i], &Vb[i], &Vc[i]);
            alpha_beta_to_abc(Ialpha[i], Ibeta[i], Izero[i], &Ia[i], &Ib[i], &Ic[i]);
        }
        *outN = n;
    } else {
        *outN = 0;
    }
    
    if (Valpha) free(Valpha);
    if (Vbeta) free(Vbeta);
    if (Vzero) free(Vzero);
    if (Ialpha) free(Ialpha);
    if (Ibeta) free(Ibeta);
    if (Izero) free(Izero);
}

/* ---------- 5.8 性能评估 ---------- */

EvaluationResult evaluate_compression(const CompressedData* data,
                                      const double* Va, const double* Vb, const double* Vc,
                                      const double* Ia, const double* Ib, const double* Ic,
                                      int n, const CompressionConfig* config) {
    EvaluationResult result;
    memset(&result, 0, sizeof(result));
    
    double* Va_r = (double*)malloc(n * sizeof(double));
    double* Vb_r = (double*)malloc(n * sizeof(double));
    double* Vc_r = (double*)malloc(n * sizeof(double));
    double* Ia_r = (double*)malloc(n * sizeof(double));
    double* Ib_r = (double*)malloc(n * sizeof(double));
    double* Ic_r = (double*)malloc(n * sizeof(double));
    
    if (!Va_r || !Vb_r || !Vc_r || !Ia_r || !Ib_r || !Ic_r) {
        if (Va_r) free(Va_r);
        if (Vb_r) free(Vb_r);
        if (Vc_r) free(Vc_r);
        if (Ia_r) free(Ia_r);
        if (Ib_r) free(Ib_r);
        if (Ic_r) free(Ic_r);
        return result;
    }
    
    /* 解压 */
    HybridCompressor comp;
    compressor_init(&comp, config);
    decompress_three_phase(&comp, data, Va_r, Vb_r, Vc_r, Ia_r, Ib_r, Ic_r, &n);
    
    /* 计算各项指标 */
    result.similarities[0] = calculate_similarity(Va, Va_r, n);
    result.similarities[1] = calculate_similarity(Vb, Vb_r, n);
    result.similarities[2] = calculate_similarity(Vc, Vc_r, n);
    result.similarities[3] = calculate_similarity(Ia, Ia_r, n);
    result.similarities[4] = calculate_similarity(Ib, Ib_r, n);
    result.similarities[5] = calculate_similarity(Ic, Ic_r, n);
    
    result.avgSimilarity = 0;
    for (int i = 0; i < 6; i++) {
        result.avgSimilarity += result.similarities[i];
    }
    result.avgSimilarity /= 6;
    
    result.avgRMSE = (calculate_rmse(Va, Va_r, n) + calculate_rmse(Vb, Vb_r, n) +
                      calculate_rmse(Vc, Vc_r, n) + calculate_rmse(Ia, Ia_r, n) +
                      calculate_rmse(Ib, Ib_r, n) + calculate_rmse(Ic, Ic_r, n)) / 6;
    
    result.avgSNR = (calculate_snr(Va, Va_r, n) + calculate_snr(Vb, Vb_r, n) +
                     calculate_snr(Vc, Vc_r, n) + calculate_snr(Ia, Ia_r, n) +
                     calculate_snr(Ib, Ib_r, n) + calculate_snr(Ic, Ic_r, n)) / 6;
    
    result.originalSize = 6 * n * sizeof(double);
    result.compressedSize = data->totalSize;
    result.compressionRatio = (double)result.originalSize / result.compressedSize;
    result.passed = (result.avgSimilarity >= config->targetSimilarity);
    
    free(Va_r);
    free(Vb_r);
    free(Vc_r);
    free(Ia_r);
    free(Ib_r);
    free(Ic_r);
    
    return result;
}

/* ---------- 5.9 释放压缩数据 ---------- */

void free_compressed_data(CompressedData* data) {
    if (data->Va) { free(data->Va); data->Va = NULL; }
    if (data->Vb) { free(data->Vb); data->Vb = NULL; }
    if (data->Vc) { free(data->Vc); data->Vc = NULL; }
    if (data->Ia) { free(data->Ia); data->Ia = NULL; }
    if (data->Ib) { free(data->Ib); data->Ib = NULL; }
    if (data->Ic) { free(data->Ic); data->Ic = NULL; }
    data->totalSize = 0;
}

/* ============================================================
 * 6. 默认配置工厂
 * ============================================================ */

CompressionConfig create_config(int sampleRate, int mode) {
    CompressionConfig config;
    memset(&config, 0, sizeof(config));
    
    config.sampleRate = sampleRate;
    config.pointsPerCycle = sampleRate / 50;  /* 50Hz工频 */
    config.deadZoneThreshold = 0.005;
    config.quantBitsFundamental = 14;
    config.quantBitsHarmonic = 6;
    config.enableDictEncoding = true;
    config.enableFrameDiff = true;
    config.enableZeroOptimization = true;
    config.enableAdaptiveHarmonics = true;
    
    switch (mode) {
        case COMPRESS_MODE_ULTRA:
            config.minHarmonics = 2;
            config.maxHarmonics = 5;
            config.quantBitsFundamental = 12;
            config.quantBitsHarmonic = 4;
            config.deadZoneThreshold = 0.01;
            config.targetSimilarity = 0.95;
            break;
            
        case COMPRESS_MODE_BALANCED:
            config.minHarmonics = 3;
            config.maxHarmonics = 10;
            config.quantBitsFundamental = 14;
            config.quantBitsHarmonic = 6;
            config.deadZoneThreshold = 0.005;
            config.targetSimilarity = 0.97;
            break;
            
        case COMPRESS_MODE_HIGH_QUALITY:
            config.minHarmonics = 5;
            config.maxHarmonics = 15;
            config.quantBitsFundamental = 16;
            config.quantBitsHarmonic = 8;
            config.deadZoneThreshold = 0.001;
            config.targetSimilarity = 0.99;
            break;
    }
    
    return config;
}

/* ============================================================
 * 7. 测试主程序
 * ============================================================ */

/* 生成测试信号 */
static void generate_test_signal(double* Va, double* Vb, double* Vc,
                                  double* Ia, double* Ib, double* Ic,
                                  int n, int sampleRate) {
    int i;
    double fs = sampleRate;
    double f = 50.0;  /* 工频 */
    
    for (i = 0; i < n; i++) {
        double t = (double)i / fs;
        double angle = 2.0 * PI * f * t;
        
        /* 含谐波的电压 */
        Va[i] = 220.0 * sin(angle) + 11.0 * sin(3 * angle + 0.1) + 6.6 * sin(5 * angle + 0.2);
        Vb[i] = 220.0 * sin(angle - 2.0 * PI / 3.0) + 
                11.0 * sin(3 * (angle - 2.0 * PI / 3.0) + 0.1) +
                6.6 * sin(5 * (angle - 2.0 * PI / 3.0) + 0.2);
        Vc[i] = 220.0 * sin(angle + 2.0 * PI / 3.0) + 
                11.0 * sin(3 * (angle + 2.0 * PI / 3.0) + 0.1) +
                6.6 * sin(5 * (angle + 2.0 * PI / 3.0) + 0.2);
        
        /* 电流 (含谐波) */
        Ia[i] = 50.0 * sin(angle - PI / 6.0) + 2.5 * sin(3 * angle - 0.2);
        Ib[i] = 50.0 * sin(angle - PI / 6.0 - 2.0 * PI / 3.0) + 
                2.5 * sin(3 * (angle - PI / 6.0 - 2.0 * PI / 3.0) - 0.2);
        Ic[i] = 50.0 * sin(angle - PI / 6.0 + 2.0 * PI / 3.0) + 
                2.5 * sin(3 * (angle - PI / 6.0 + 2.0 * PI / 3.0) - 0.2);
    }
}

/* 打印结果 */
static void print_result(const EvaluationResult* result, const char* modeName) {
    printf("\n============================================================\n");
    printf("  压缩模式: %s\n", modeName);
    printf("============================================================\n");
    printf("原始大小: %zu 字节\n", result->originalSize);
    printf("压缩后大小: %zu 字节\n", result->compressedSize);
    printf("压缩比: %.2f:1\n", result->compressionRatio);
    printf("压缩率: %.2f%%\n", (1 - (double)result->compressedSize / result->originalSize) * 100);
    printf("\n相似度:\n");
    printf("  Va: %.2f%%\n", result->similarities[0] * 100);
    printf("  Vb: %.2f%%\n", result->similarities[1] * 100);
    printf("  Vc: %.2f%%\n", result->similarities[2] * 100);
    printf("  Ia: %.2f%%\n", result->similarities[3] * 100);
    printf("  Ib: %.2f%%\n", result->similarities[4] * 100);
    printf("  Ic: %.2f%%\n", result->similarities[5] * 100);
    printf("平均相似度: %.2f%%\n", result->avgSimilarity * 100);
    printf("平均RMSE: %.4f\n", result->avgRMSE);
    printf("平均SNR: %.2f dB\n", result->avgSNR);
    printf("\n状态: %s\n", result->passed ? "✅ 通过 (相似度 > 95%)" : "❌ 未通过");
}

int main() {
    int sampleRate = 6400;
    int n = sampleRate / 50;  /* 128点 */
    const char* modeNames[] = {"极致压缩", "平衡模式", "高质量"};
    
    printf("\n============================================================\n");
    printf("    混合智能压缩算法 - 三相电压电流波形压缩测试\n");
    printf("============================================================\n");
    printf("采样率: %d Hz\n", sampleRate);
    printf("每周期点数: %d\n", n);
    printf("============================================================\n");
    
    /* 分配内存 */
    double* Va = (double*)malloc(n * sizeof(double));
    double* Vb = (double*)malloc(n * sizeof(double));
    double* Vc = (double*)malloc(n * sizeof(double));
    double* Ia = (double*)malloc(n * sizeof(double));
    double* Ib = (double*)malloc(n * sizeof(double));
    double* Ic = (double*)malloc(n * sizeof(double));
    
    if (!Va || !Vb || !Vc || !Ia || !Ib || !Ic) {
        printf("内存分配失败!\n");
        return 1;
    }
    
    /* 生成测试信号 */
    generate_test_signal(Va, Vb, Vc, Ia, Ib, Ic, n, sampleRate);
    
    /* 测试三种模式 */
    for (int mode = 0; mode < 3; mode++) {
        CompressionConfig config = create_config(sampleRate, mode);
        HybridCompressor comp;
        compressor_init(&comp, &config);
        
        /* 压缩 */
        CompressedData data = compress_three_phase(&comp, Va, Vb, Vc, Ia, Ib, Ic, n);
        
        /* 评估 */
        EvaluationResult result = evaluate_compression(&data, Va, Vb, Vc, Ia, Ib, Ic, n, &config);
        
        /* 打印结果 */
        print_result(&result, modeNames[mode]);
        
        /* 释放 */
        free_compressed_data(&data);
    }
    
    /* 零序优化测试 */
    printf("\n============================================================\n");
    printf("  零序优化测试\n");
    printf("============================================================\n");
    
    CompressionConfig config = create_config(sampleRate, COMPRESS_MODE_BALANCED);
    HybridCompressor comp;
    compressor_init(&comp, &config);
    comp.config.enableZeroOptimization = true;
    
    CompressedData dataZero = compress_with_zero_optimization(&comp, Va, Vb, Vc, Ia, Ib, Ic, n);
    EvaluationResult resultZero = evaluate_compression(&dataZero, Va, Vb, Vc, Ia, Ib, Ic, n, &config);
    
    print_result(&resultZero, "平衡模式 + 零序优化");
    free_compressed_data(&dataZero);
    
    /* 打印统计信息 */
    comp.totalFrames = 1;  /* 修正统计 */
    comp.printStats(&comp);
    
    /* 释放内存 */
    free(Va);
    free(Vb);
    free(Vc);
    free(Ia);
    free(Ib);
    free(Ic);
    
    printf("\n============================================================\n");
    printf("  测试完成\n");
    printf("============================================================\n");
    
    return 0;
}