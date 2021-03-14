NAME := sort

all: $(NAME).c
	$(CC) $(NAME).c -o $(NAME).out -lrt

test: all
	python3 generator.py -f test1.txt -c 10000 -m 10000
	python3 generator.py -f test2.txt -c 10000 -m 10000
	python3 generator.py -f test3.txt -c 10000 -m 10000
	python3 generator.py -f test4.txt -c 10000 -m 10000
	python3 generator.py -f test5.txt -c 10000 -m 10000
	python3 generator.py -f test6.txt -c 10000 -m 10000
	./$(NAME).out test{1,2,3,4,5,6}.txt
	python3 checker.py -f result.txt

clean: 
	rm -f result.txt test*.txt *.out
