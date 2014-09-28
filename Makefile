SRCS = httpserver.c http.c stream.c
HEADS = http.h stream.h

httpserver: $(SRCS) $(HEADS)
	gcc -O2 $(SRCS) -o $@

clean:
	rm -f httpserver

pack: report.txt $(SRCS) $(HEADS)
	cd ..; tar zcvf report7.tar.gz $(foreach f,$^,report7/$(f))
