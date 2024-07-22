CC = g++
LIBS = -lws2_32 -lwsock32
CFLAGS = 

OBJECTS = main.o
BUILD_DIR = build

# Script
BUILD_OBJECTS = $(addprefix $(BUILD_DIR)/,$(OBJECTS))

$(BUILD_DIR)/%.o: %.cpp $(BUILD_DIR)/
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/:
	mkdir build

$(BUILD_DIR)/windows.exe: $(BUILD_OBJECTS)
	$(CC) $(CFLAGS) $(BUILD_OBJECTS) -o $(BUILD_DIR)/windows.exe $(LIBS)

run: $(BUILD_DIR)/windows.exe
	$(BUILD_DIR)/windows.exe

clean:
	rmdir /s /q $(BUILD_DIR)
