CC=gcc
CFLAGS=-O2 -Wall -Wextra
LDFLAGS=-lm -ldl
SRC_COMPRESS=compress/compress_data.c
BIN_COMPRESS=compress_data
SRC_TOOL=file_tool.c
BIN_TOOL=nilm_tool

all: $(BIN_COMPRESS) $(BIN_TOOL)

$(BIN_COMPRESS): $(SRC_COMPRESS)
	$(CC) $(CFLAGS) $(SRC_COMPRESS) -o $(BIN_COMPRESS) $(LDFLAGS)

$(BIN_TOOL): $(SRC_TOOL) $(SRC_COMPRESS) entropy.c
	$(CC) $(CFLAGS) $(SRC_TOOL) -o $(BIN_TOOL) $(LDFLAGS)

run: $(BIN_COMPRESS)
	./$(BIN_COMPRESS)

run-tool: $(BIN_TOOL)
	./$(BIN_TOOL)

clean:
	rm -f $(BIN_COMPRESS) $(BIN_TOOL) /tmp/compress_test
	rm -rf out/

test: $(BIN_COMPRESS)
	./$(BIN_COMPRESS) | tee test_result.txt
	@echo "测试完成，结果保存在 test_result.txt"

test-file: $(BIN_TOOL)
	./$(BIN_TOOL) --mode 1

setup:
	chmod +x setup.sh
	./setup.sh

dirs:
	mkdir -p data out/compressed out/reconstructed
