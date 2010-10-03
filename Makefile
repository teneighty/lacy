
all: lacy-pages

lacy-pages:
	lacy index.html
	mv _output _site

clean:
	rm -rf _site
