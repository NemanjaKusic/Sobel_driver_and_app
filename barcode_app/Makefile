CC = g++
PROJECT = barcode_app
SRC = barcode_app.cpp

LIBS = `pkg-config opencv4 --cflags --libs`

PROJECT: $(SRC)
	$(CC) $(SRC) -o $(PROJECT) $(LIBS)

run:
	./$(PROJECT) ../data/barcode_01.jpg

clean:
	@rm -f $(PROJECT) *.jpg gradx.txt grady.txt
	@echo "Clean done.."
