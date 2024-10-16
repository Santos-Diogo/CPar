CPP = g++ -Wall -pg -O2
SRCS = main.cpp fluid_solver.cpp EventManager.cpp
BIN_DIR = ./bin
RESULTS_DIR = ./testResults
BIN = $(BIN_DIR)/fluid_sim.bin
INPUT_FILE = events.txt

all: $(BIN)

$(BIN): $(SRCS)
	$(CPP) $(SRCS) -o $(BIN)

setup:
	@echo Setting Up...
	mkdir -p $(BIN_DIR)
	mkdir -p $(RESULTS_DIR)

test: all
	@echo Running Tests...
	@$(BIN) < $(INPUT_FILE)
	@gprof $(BIN) gmon.out > $(RESULTS_DIR)/analysis.txt
	@sleep 1
	@less $(RESULTS_DIR)/analysis.txt

clean:
	@echo Cleaning up...
	rm -f $(BIN) gmon.out
	@echo Done.
