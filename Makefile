CC=gcc
CFLAGS=-O2 -Wall -Wextra
LDFLAGS=-lm
SRC=compress/compress_data.c
BIN=compress_data

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) /tmp/compress_test

test: $(BIN)
	./$(BIN) | tee test_result.txt
	@echo "测试完成，结果保存在 test_result.txt"

setup:
	chmod +x setup.sh
	./setup.sh
