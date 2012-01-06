
all: site serve

site:
	lacy *.mkd

serve:
	cd _output; python -m SimpleHTTPServer 4000

clean:
	rm -rf _output

.PHONY: all site serve clean
