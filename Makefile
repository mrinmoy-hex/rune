CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99

SRC_DIR = src
BUILD_DIR = build

$(BUILD_DIR)/rune: $(SRC_DIR)/rune.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@