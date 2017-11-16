TARGET = test_suite
TMP = $(wildcard ../*.c)
EXECS = $(shell cd ../ && grep -rnw --include \*.c "main" . | cut -d: -f1 | cut -d/ -f2 | awk '{print "../" $$0}') 
TEST_DEPS = $(shell cat test_dependencies)

SRCS = $(filter-out $(EXECS), $(TMP)) 
DEBUG = -g

CFLAGS=-std=c99 -Wall

all: $(TARGET)

debug: 
	echo $(TEST_DEPS)

$(TARGET): $(TEST_DEPS) 
	gcc -g $(CFLAGS) -o $@ $^ test_suite.c `pkg-config --cflags --libs check` 

clean:
	rm -f $(TARGET) $(OBJS)

