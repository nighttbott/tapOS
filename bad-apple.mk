# Bad Apple data. The player itself is the user program `badapple` (see Makefile); play it with `run badapple`.

APPLE_DATA = $(OUTPUT_FOLDER)/apple.dat

.PHONY: apple-data apple-insert test-badapple
apple-data:
	@test -n "$(VIDEO)" || { echo "usage: make apple-data VIDEO=bad_apple.mp4"; exit 1; }
	@mkdir -p $(OUTPUT_FOLDER)
	python3 tools/badapple_encode.py $(VIDEO) $(APPLE_DATA)

apple-insert: apple-data inserter | disk
	./$(OUTPUT_FOLDER)/inserter -f $(APPLE_DATA) 1 $(OUTPUT_FOLDER)/$(DISK_NAME).bin

test-badapple:
	@bash tests/host/run_badapple_test.sh
