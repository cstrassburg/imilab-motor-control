all: cross

cross:
	test -d dist || mkdir dist
	test -d dist/static || mkdir dist/static
	GOOS=linux GOARCH=arm go build -o dist/web_control
	cp static/* dist/static/

test:
	test -d test || mkdir test
	go build -o test/web_control

clean:
	rm -rf dist test
