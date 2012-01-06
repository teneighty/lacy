
all: site serve

site:
	lacy *.mkd
	mv _output/* ..
	rmdir _output

serve:
	cd ..; python -m SimpleHTTPServer 4000

clean:
	rm ../*.*

.PHONY: all site serve clean
