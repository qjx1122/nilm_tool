/**
 * NILM 文件压缩工具 - 支持 data/ -> out/compressed/ -> out/reconstructed/
 * v0.6: 单周期 vs 多周期对比验证，统计每个文件在两种模式下的压缩比和相似度
 */

#define COMPRESSOR_LIB
#include "compress/compress_data.c"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>

#define OUT_COMPRESSED_DIR "out/compressed"
#define OUT_RECONSTRUCTED_DIR "out/reconstructed"
#define OUT_REPORT_DIR "out"
#define DATA_DIR "data"

typedef struct {
    char** timestamps;
    double *Va, *Vb, *Vc, *Ia, *Ib, *Ic;
    int n;
    int sampleRate;
} WaveData;

typedef struct {
    char filename[256];
    int rows;
    int sampleRate;
    int pointsPerCycle;
    int numFrames;
    size_t originalBytes;
    size_t compressedBytes;
    double compressionRatio;
    double avgSimilarity;
    double similarities[6];
    double avgRMSE;
    double avgSNR;
    double perChannelRMSE[6];
    double perChannelSNR[6];
} FileMetrics;

typedef struct {
    FileMetrics single; // 单周期：frameDiff=false
    FileMetrics multi;  // 多周期：frameDiff=true
} CombinedMetrics;

static int ensure_dir(const char* path){
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for(char* p=tmp+1; *p; p++){
        if(*p=='/'){ *p='\0'; mkdir(tmp, 0755); *p='/'; }
    }
    mkdir(tmp, 0755);
    return 0;
}

static double parse_sec_from_ts(const char* ts){
    const char* last_space = strrchr(ts, ' ');
    if(!last_space) return 0;
    const char* time_part = last_space+1;
    const char* colon1 = strchr(time_part, ':');
    if(!colon1) return 0;
    const char* colon2 = strchr(colon1+1, ':');
    if(!colon2) return 0;
    return atof(colon2+1);
}

static int estimate_sample_rate_from_csv(const char* path){
    FILE* f = fopen(path, "r");
    if(!f) return 10000;
    char line[2048];
    if(!fgets(line, sizeof(line), f)){ fclose(f); return 10000; }
    if(!fgets(line, sizeof(line), f)){ fclose(f); return 10000; }
    char ts1[128]={0};
    sscanf(line, "%127[^,]", ts1);
    if(!fgets(line, sizeof(line), f)){ fclose(f); return 10000; }
    char ts2[128]={0};
    sscanf(line, "%127[^,]", ts2);
    fclose(f);
    double s1 = parse_sec_from_ts(ts1);
    double s2 = parse_sec_from_ts(ts2);
    double diff = s2 - s1;
    if(diff<0) diff+=60;
    if(diff<1e-9 || diff>1) return 10000;
    int sr = (int)round(1.0/diff);
    if(sr<1000 || sr>50000) return 10000;
    return sr;
}

static WaveData* read_csv_wave(const char* path){
    FILE* f = fopen(path, "r");
    if(!f){ perror(path); return NULL; }
    char line[4096];
    if(!fgets(line, sizeof(line), f)){ fclose(f); return NULL; }
    int idx_ts=-1, idx_ua=-1, idx_ia=-1, idx_ub=-1, idx_ib=-1, idx_uc=-1, idx_ic=-1;
    {
        char header_copy[4096];
        strncpy(header_copy, line, sizeof(header_copy));
        char* token = strtok(header_copy, ",");
        int col=0;
        while(token){
            while(*token==' '||*token=='\t') token++;
            char* end=token+strlen(token)-1;
            while(end>token && (*end==' '||*end=='\r'||*end=='\n')){*end='\0'; end--;}
            if(strcmp(token,"timestamp")==0) idx_ts=col;
            else if(strcmp(token,"UA")==0) idx_ua=col;
            else if(strcmp(token,"IA")==0) idx_ia=col;
            else if(strcmp(token,"UB")==0) idx_ub=col;
            else if(strcmp(token,"IB")==0) idx_ib=col;
            else if(strcmp(token,"UC")==0) idx_uc=col;
            else if(strcmp(token,"IC")==0) idx_ic=col;
            else if(strcmp(token,"Va")==0) idx_ua=col;
            else if(strcmp(token,"Ia")==0) idx_ia=col;
            else if(strcmp(token,"Vb")==0) idx_ub=col;
            else if(strcmp(token,"Ib")==0) idx_ib=col;
            else if(strcmp(token,"Vc")==0) idx_uc=col;
            else if(strcmp(token,"Ic")==0) idx_ic=col;
            token=strtok(NULL, ",");
            col++;
        }
    }
    if(idx_ts<0 || idx_ua<0 || idx_ia<0 || idx_ub<0 || idx_ib<0 || idx_uc<0 || idx_ic<0){
        if(idx_ts<0) idx_ts=0;
        if(idx_ua<0) idx_ua=1;
        if(idx_ia<0) idx_ia=2;
        if(idx_ub<0) idx_ub=3;
        if(idx_ib<0) idx_ib=4;
        if(idx_uc<0) idx_uc=5;
        if(idx_ic<0) idx_ic=6;
    }
    int capacity=1024;
    WaveData* wd=(WaveData*)calloc(1,sizeof(WaveData));
    wd->timestamps=(char**)malloc(capacity*sizeof(char*));
    wd->Va=(double*)malloc(capacity*sizeof(double));
    wd->Vb=(double*)malloc(capacity*sizeof(double));
    wd->Vc=(double*)malloc(capacity*sizeof(double));
    wd->Ia=(double*)malloc(capacity*sizeof(double));
    wd->Ib=(double*)malloc(capacity*sizeof(double));
    wd->Ic=(double*)malloc(capacity*sizeof(double));
    wd->n=0;
    while(fgets(line, sizeof(line), f)){
        if(strlen(line)<5) continue;
        char* fields[32];
        int nfield=0;
        char* p=line;
        char* start=p;
        while(*p && nfield<32){
            if(*p==',' || *p=='\n' || *p=='\r'){
                *p='\0';
                fields[nfield++]=start;
                start=p+1;
            }
            p++;
        }
        if(nfield<=idx_ic) continue;
        if(wd->n>=capacity){
            capacity*=2;
            wd->timestamps=(char**)realloc(wd->timestamps, capacity*sizeof(char*));
            wd->Va=(double*)realloc(wd->Va, capacity*sizeof(double));
            wd->Vb=(double*)realloc(wd->Vb, capacity*sizeof(double));
            wd->Vc=(double*)realloc(wd->Vc, capacity*sizeof(double));
            wd->Ia=(double*)realloc(wd->Ia, capacity*sizeof(double));
            wd->Ib=(double*)realloc(wd->Ib, capacity*sizeof(double));
            wd->Ic=(double*)realloc(wd->Ic, capacity*sizeof(double));
        }
        wd->timestamps[wd->n]=strdup(fields[idx_ts]);
        wd->Va[wd->n]=atof(fields[idx_ua]);
        wd->Ia[wd->n]=atof(fields[idx_ia]);
        wd->Vb[wd->n]=atof(fields[idx_ub]);
        wd->Ib[wd->n]=atof(fields[idx_ib]);
        wd->Vc[wd->n]=atof(fields[idx_uc]);
        wd->Ic[wd->n]=atof(fields[idx_ic]);
        wd->n++;
    }
    fclose(f);
    wd->sampleRate = estimate_sample_rate_from_csv(path);
    printf("读取 %s: %d 行, 估算采样率 %d Hz\n", path, wd->n, wd->sampleRate);
    return wd;
}

static void free_wave_data(WaveData* wd){
    if(!wd) return;
    for(int i=0;i<wd->n;i++) free(wd->timestamps[i]);
    free(wd->timestamps);
    free(wd->Va); free(wd->Vb); free(wd->Vc);
    free(wd->Ia); free(wd->Ib); free(wd->Ic);
    free(wd);
}

/* 核心：压缩并解压一个文件，支持单周期/多周期切换 */
static FileMetrics verify_file(const char* inputPath, const char* compOutPath, const char* reconOutPath, int mode, bool useZero, bool multiCycle){
    FileMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    strncpy(metrics.filename, inputPath, sizeof(metrics.filename)-1);

    WaveData* wd = read_csv_wave(inputPath);
    if(!wd) return metrics;
    if(wd->n<10){ printf("文件 %s 行数过少，跳过\n", inputPath); free_wave_data(wd); return metrics; }

    int ppc = wd->sampleRate / 50;
    if(ppc>MAX_POINTS_PER_CYCLE) ppc=MAX_POINTS_PER_CYCLE;
    if(ppc<16) ppc=16;
    metrics.rows = wd->n;
    metrics.sampleRate = wd->sampleRate;
    metrics.pointsPerCycle = ppc;
    metrics.numFrames = wd->n / ppc;
    metrics.originalBytes = (size_t)wd->n * 6 * sizeof(double);

    CompressionConfig config = create_config(wd->sampleRate, mode);
    config.enableZeroOptimization = useZero;
    config.enableFrameDiff = multiCycle; // 单周期=false, 多周期=true
    config.enableDictEncoding = true;

    FILE* fcomp = fopen(compOutPath, "wb");
    FILE* frec = fopen(reconOutPath, "w");
    if(!fcomp || !frec){
        if(fcomp) fclose(fcomp);
        if(frec) fclose(frec);
        free_wave_data(wd);
        return metrics;
    }
    fwrite("NILM",1,4,fcomp);
    uint32_t version=1;
    fwrite(&version,4,1,fcomp);
    int32_t sr=wd->sampleRate, ppc_i=ppc, numFrames=metrics.numFrames, flags=0;
    if(config.enableZeroOptimization) flags|=1;
    if(config.enableDictEncoding) flags|=2;
    if(config.enableFrameDiff) flags|=4;
    fwrite(&sr,4,1,fcomp);
    fwrite(&ppc_i,4,1,fcomp);
    fwrite(&numFrames,4,1,fcomp);
    fwrite(&flags,4,1,fcomp);
    fprintf(frec, "timestamp,UA,IA,UB,IB,UC,IC\n");

    HybridCompressor compEnc, compDec;
    compressor_init(&compEnc, &config);
    compressor_init(&compDec, &config);

    size_t totalCompressed = 0;
    double totalSimAll=0, totalRMSEAll=0, totalSNRAll=0;
    double simPerCh[6]={0}, rmsePerCh[6]={0}, snrPerCh[6]={0};
    int frameCount=0;

    for(int fr=0; fr<metrics.numFrames; fr++){
        int off=fr*ppc;
        CompressedData cd = compress_three_phase(&compEnc,
            wd->Va+off, wd->Vb+off, wd->Vc+off,
            wd->Ia+off, wd->Ib+off, wd->Ic+off,
            ppc);
        uint32_t sizes[6]={ (uint32_t)cd.sizeVa, (uint32_t)cd.sizeVb, (uint32_t)cd.sizeVc,
                            (uint32_t)cd.sizeIa, (uint32_t)cd.sizeIb, (uint32_t)cd.sizeIc };
        fwrite(sizes,4,6,fcomp);
        if(cd.sizeVa) fwrite(cd.Va,1,cd.sizeVa,fcomp);
        if(cd.sizeVb) fwrite(cd.Vb,1,cd.sizeVb,fcomp);
        if(cd.sizeVc) fwrite(cd.Vc,1,cd.sizeVc,fcomp);
        if(cd.sizeIa) fwrite(cd.Ia,1,cd.sizeIa,fcomp);
        if(cd.sizeIb) fwrite(cd.Ib,1,cd.sizeIb,fcomp);
        if(cd.sizeIc) fwrite(cd.Ic,1,cd.sizeIc,fcomp);
        totalCompressed+= cd.totalSize + 6*4;

        double *Va_r=malloc(ppc*sizeof(double));
        double *Vb_r=malloc(ppc*sizeof(double));
        double *Vc_r=malloc(ppc*sizeof(double));
        double *Ia_r=malloc(ppc*sizeof(double));
        double *Ib_r=malloc(ppc*sizeof(double));
        double *Ic_r=malloc(ppc*sizeof(double));
        int outN;
        decompress_three_phase(&compDec, &cd, Va_r, Vb_r, Vc_r, Ia_r, Ib_r, Ic_r, &outN);

        double sims[6];
        sims[0]=calculate_similarity(wd->Va+off, Va_r, ppc);
        sims[1]=calculate_similarity(wd->Vb+off, Vb_r, ppc);
        sims[2]=calculate_similarity(wd->Vc+off, Vc_r, ppc);
        sims[3]=calculate_similarity(wd->Ia+off, Ia_r, ppc);
        sims[4]=calculate_similarity(wd->Ib+off, Ib_r, ppc);
        sims[5]=calculate_similarity(wd->Ic+off, Ic_r, ppc);
        double rmses[6];
        rmses[0]=calculate_rmse(wd->Va+off, Va_r, ppc);
        rmses[1]=calculate_rmse(wd->Vb+off, Vb_r, ppc);
        rmses[2]=calculate_rmse(wd->Vc+off, Vc_r, ppc);
        rmses[3]=calculate_rmse(wd->Ia+off, Ia_r, ppc);
        rmses[4]=calculate_rmse(wd->Ib+off, Ib_r, ppc);
        rmses[5]=calculate_rmse(wd->Ic+off, Ic_r, ppc);
        double snrs[6];
        snrs[0]=calculate_snr(wd->Va+off, Va_r, ppc);
        snrs[1]=calculate_snr(wd->Vb+off, Vb_r, ppc);
        snrs[2]=calculate_snr(wd->Vc+off, Vc_r, ppc);
        snrs[3]=calculate_snr(wd->Ia+off, Ia_r, ppc);
        snrs[4]=calculate_snr(wd->Ib+off, Ib_r, ppc);
        snrs[5]=calculate_snr(wd->Ic+off, Ic_r, ppc);

        for(int ch=0; ch<6; ch++){
            simPerCh[ch]+=sims[ch];
            rmsePerCh[ch]+=rmses[ch];
            snrPerCh[ch]+=snrs[ch];
        }
        totalSimAll+=(sims[0]+sims[1]+sims[2]+sims[3]+sims[4]+sims[5])/6.0;
        totalRMSEAll+=(rmses[0]+rmses[1]+rmses[2]+rmses[3]+rmses[4]+rmses[5])/6.0;
        totalSNRAll+=(snrs[0]+snrs[1]+snrs[2]+snrs[3]+snrs[4]+snrs[5])/6.0;
        frameCount++;

        for(int i=0;i<ppc;i++){
            int idx=off+i;
            if(idx>=wd->n) break;
            fprintf(frec, "%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",
                wd->timestamps[idx], Va_r[i], Ia_r[i], Vb_r[i], Ib_r[i], Vc_r[i], Ic_r[i]);
        }
        free(Va_r); free(Vb_r); free(Vc_r); free(Ia_r); free(Ib_r); free(Ic_r);
        free_compressed_data(&cd);
    }
    fclose(fcomp);
    fclose(frec);
    metrics.compressedBytes = totalCompressed + 20;
    metrics.compressionRatio = (double)metrics.originalBytes / (metrics.compressedBytes?metrics.compressedBytes:1);
    if(frameCount>0){
        metrics.avgSimilarity = totalSimAll / frameCount;
        metrics.avgRMSE = totalRMSEAll / frameCount;
        metrics.avgSNR = totalSNRAll / frameCount;
        for(int ch=0; ch<6; ch++){
            metrics.similarities[ch]=simPerCh[ch]/frameCount;
            metrics.perChannelRMSE[ch]=rmsePerCh[ch]/frameCount;
            metrics.perChannelSNR[ch]=snrPerCh[ch]/frameCount;
        }
    }
    free_wave_data(wd);
    return metrics;
}

/* 单文件同时跑单周期和多周期，返回两者对比 */
static CombinedMetrics verify_both_modes(const char* inputPath, int mode, bool useZero){
    CombinedMetrics combined;
    memset(&combined, 0, sizeof(combined));
    char baseName[512];
    const char* slash=strrchr(inputPath,'/');
    const char* name=slash?slash+1:inputPath;
    strncpy(baseName, name, sizeof(baseName));
    char* ext=strrchr(baseName,'.'); if(ext) *ext='\0';

    char compSingle[1024], reconSingle[1024];
    char compMulti[1024], reconMulti[1024];
    snprintf(compSingle, sizeof(compSingle), "%s/%s_single.bin", OUT_COMPRESSED_DIR, baseName);
    snprintf(reconSingle, sizeof(reconSingle), "%s/%s_single_reconstructed.csv", OUT_RECONSTRUCTED_DIR, baseName);
    snprintf(compMulti, sizeof(compMulti), "%s/%s_multi.bin", OUT_COMPRESSED_DIR, baseName);
    snprintf(reconMulti, sizeof(reconMulti), "%s/%s_multi_reconstructed.csv", OUT_RECONSTRUCTED_DIR, baseName);

    printf("\n--- 单周期压缩 (frameDiff=0) ---\n");
    combined.single = verify_file(inputPath, compSingle, reconSingle, mode, useZero, false);
    printf("单周期结果: 压缩比 %.2f:1 相似度 %.4f%%\n", combined.single.compressionRatio, combined.single.avgSimilarity*100);

    printf("\n--- 多周期压缩 (frameDiff=1 + 0xFF重复) ---\n");
    combined.multi = verify_file(inputPath, compMulti, reconMulti, mode, useZero, true);
    printf("多周期结果: 压缩比 %.2f:1 相似度 %.4f%%\n", combined.multi.compressionRatio, combined.multi.avgSimilarity*100);

    return combined;
}

static void process_data_directory_both(int mode, bool useZero){
    DIR* d = opendir(DATA_DIR);
    if(!d){ printf("data 目录不存在\n"); ensure_dir(DATA_DIR); return; }
    char fileList[64][512];
    int fileCount=0;
    struct dirent* entry;
    while((entry=readdir(d))!=NULL && fileCount<64){
        if(entry->d_name[0]=='.') continue;
        char* dot=strrchr(entry->d_name, '.');
        if(!dot) continue;
        if(strcmp(dot, ".csv")!=0 && strcmp(dot, ".CSV")!=0) continue;
        strncpy(fileList[fileCount], entry->d_name, 511);
        fileCount++;
    }
    closedir(d);
    if(fileCount==0){ printf("data/ 下未找到 CSV\n"); return; }

    ensure_dir(OUT_REPORT_DIR);
    char reportPath[1024];
    snprintf(reportPath, sizeof(reportPath), "%s/verification_report_single_multi.csv", OUT_REPORT_DIR);
    FILE* freport = fopen(reportPath, "w");
    if(freport){
        fprintf(freport, "filename,rows,sampleRate,ppc,numFrames,originalBytes,"
                "single_compBytes,single_ratio,single_sim,sim_Va_single,sim_Ia_single,rmse_single,snr_single,"
                "multi_compBytes,multi_ratio,multi_sim,sim_Va_multi,sim_Ia_multi,rmse_multi,snr_multi,ratio_gain\n");
    }
    char reportTxtPath[1024];
    snprintf(reportTxtPath, sizeof(reportTxtPath), "%s/verification_report_single_multi.txt", OUT_REPORT_DIR);
    FILE* freportTxt = fopen(reportTxtPath, "w");
    if(freportTxt){
        fprintf(freportTxt, "NILM 单周期 vs 多周期 压缩验证报告\n");
        fprintf(freportTxt, "========================================\n");
        fprintf(freportTxt, "模式: %s 零序: %s 时间: %s %s\n", mode==0?"ULTRA":mode==1?"BALANCED":"HIGH", useZero?"启用":"禁用", __DATE__, __TIME__);
        fprintf(freportTxt, "========================================\n\n");
    }

    double totalOrig=0, totalSingleComp=0, totalMultiComp=0, totalSingleSim=0, totalMultiSim=0;
    for(int i=0;i<fileCount;i++){
        char inputPath[1024];
        snprintf(inputPath, sizeof(inputPath), "%s/%s", DATA_DIR, fileList[i]);
        printf("\n========== 文件 [%d/%d]: %s ==========\n", i+1, fileCount, inputPath);
        CombinedMetrics cm = verify_both_modes(inputPath, mode, useZero);

        totalOrig+=cm.single.originalBytes;
        totalSingleComp+=cm.single.compressedBytes;
        totalMultiComp+=cm.multi.compressedBytes;
        totalSingleSim+=cm.single.avgSimilarity;
        totalMultiSim+=cm.multi.avgSimilarity;

        if(freport){
            double gain = cm.multi.compressionRatio / (cm.single.compressionRatio?cm.single.compressionRatio:1);
            fprintf(freport, "%s,%d,%d,%d,%d,%zu,%zu,%.2f,%.6f,%.6f,%.6f,%.6f,%.2f,%zu,%.2f,%.6f,%.6f,%.6f,%.6f,%.2f,%.2f\n",
                fileList[i], cm.single.rows, cm.single.sampleRate, cm.single.pointsPerCycle, cm.single.numFrames,
                cm.single.originalBytes,
                cm.single.compressedBytes, cm.single.compressionRatio, cm.single.avgSimilarity,
                cm.single.similarities[0], cm.single.similarities[3], cm.single.avgRMSE, cm.single.avgSNR,
                cm.multi.compressedBytes, cm.multi.compressionRatio, cm.multi.avgSimilarity,
                cm.multi.similarities[0], cm.multi.similarities[3], cm.multi.avgRMSE, cm.multi.avgSNR,
                gain);
        }
        if(freportTxt){
            fprintf(freportTxt, "文件: %s (%d 行, %d Hz, %d 点/周期, %d 帧)\n", fileList[i], cm.single.rows, cm.single.sampleRate, cm.single.pointsPerCycle, cm.single.numFrames);
            fprintf(freportTxt, "  原始大小: %zu 字节 (%.2f MB)\n", cm.single.originalBytes, cm.single.originalBytes/1024.0/1024.0);
            fprintf(freportTxt, "  单周期: 压缩 %zu 字节  压缩比 %.2f:1  相似度 %.4f%%  RMSE %.4f  SNR %.2f dB\n",
                cm.single.compressedBytes, cm.single.compressionRatio, cm.single.avgSimilarity*100, cm.single.avgRMSE, cm.single.avgSNR);
            fprintf(freportTxt, "    通道: Va %.4f%% Ia %.4f%%\n", cm.single.similarities[0]*100, cm.single.similarities[3]*100);
            fprintf(freportTxt, "  多周期: 压缩 %zu 字节  压缩比 %.2f:1  相似度 %.4f%%  RMSE %.4f  SNR %.2f dB\n",
                cm.multi.compressedBytes, cm.multi.compressionRatio, cm.multi.avgSimilarity*100, cm.multi.avgRMSE, cm.multi.avgSNR);
            fprintf(freportTxt, "    通道: Va %.4f%% Ia %.4f%%\n", cm.multi.similarities[0]*100, cm.multi.similarities[3]*100);
            fprintf(freportTxt, "  增益: 多周期/单周期 = %.2fx (压缩比提升)\n", cm.multi.compressionRatio/(cm.single.compressionRatio?cm.single.compressionRatio:1));
            fprintf(freportTxt, "\n");
        }
    }

    if(fileCount>0){
        double avgSingleRatio = totalOrig / (totalSingleComp?totalSingleComp:1);
        double avgMultiRatio = totalOrig / (totalMultiComp?totalMultiComp:1);
        printf("\n========================================\n");
        printf("汇总 %d 文件 总原始 %.2f MB\n", fileCount, totalOrig/1024/1024);
        printf("单周期 总压缩 %.2f MB 平均压缩比 %.2f:1 平均相似度 %.4f%%\n", totalSingleComp/1024/1024, avgSingleRatio, totalSingleSim/fileCount*100);
        printf("多周期 总压缩 %.2f MB 平均压缩比 %.2f:1 平均相似度 %.4f%%\n", totalMultiComp/1024/1024, avgMultiRatio, totalMultiSim/fileCount*100);
        printf("多周期相对增益: %.2fx\n", avgMultiRatio/(avgSingleRatio?avgSingleRatio:1));
        printf("报告: %s, %s\n", reportPath, reportTxtPath);
        if(freportTxt){
            fprintf(freportTxt, "========================================\n");
            fprintf(freportTxt, "汇总 %d 文件 总原始 %.2f MB\n", fileCount, totalOrig/1024/1024);
            fprintf(freportTxt, "单周期 总压缩 %.2f MB 平均压缩比 %.2f:1 平均相似度 %.4f%%\n", totalSingleComp/1024/1024, avgSingleRatio, totalSingleSim/fileCount*100);
            fprintf(freportTxt, "多周期 总压缩 %.2f MB 平均压缩比 %.2f:1 平均相似度 %.4f%%\n", totalMultiComp/1024/1024, avgMultiRatio, totalMultiSim/fileCount*100);
        }
    }
    if(freport) fclose(freport);
    if(freportTxt) fclose(freportTxt);
}

static void print_usage(const char* prog){
    printf("用法: %s [选项]\n", prog);
    printf("  --mode <0|1|2>  压缩模式 0=极致 1=平衡(默认) 2=高质量\n");
    printf("  --zero-opt <0|1> 零序优化\n");
    printf("  --input <file> 单文件\n");
}

int main(int argc, char* argv[]){
    int mode=1;
    bool useZero=true;
    const char* singleInput=NULL;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--mode")==0 && i+1<argc) mode=atoi(argv[++i]);
        else if(strcmp(argv[i],"--zero-opt")==0 && i+1<argc) useZero=atoi(argv[++i])!=0;
        else if(strcmp(argv[i],"--input")==0 && i+1<argc) singleInput=argv[++i];
        else if(strcmp(argv[i],"--help")==0){ print_usage(argv[0]); return 0; }
    }
    ensure_dir(OUT_COMPRESSED_DIR);
    ensure_dir(OUT_RECONSTRUCTED_DIR);
    ensure_dir(DATA_DIR);
    if(singleInput){
        printf("单文件双模式验证: %s\n", singleInput);
        CombinedMetrics cm = verify_both_modes(singleInput, mode, useZero);
        printf("\n单文件汇总:\n单周期 %.2f:1 %.4f%%  多周期 %.2f:1 %.4f%%\n",
            cm.single.compressionRatio, cm.single.avgSimilarity*100,
            cm.multi.compressionRatio, cm.multi.avgSimilarity*100);
    }else{
        process_data_directory_both(mode, useZero);
    }
    return 0;
}
