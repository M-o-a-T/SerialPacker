#!/usr/bin/make -f

PACKAGE = serialpacker
PYTEST ?= python3 -mpytest

CODE=serialpacker.py
TESTS=test.py
SETUP=

test: statictest pytest
statictest:
	black --check $(CODE) $(TESTS) $(SETUP)
	python3 -misort --check $(CODE) $(TESTS) $(SETUP)
	flake8p $(CODE) $(TESTS) $(SETUP)
	pylint $(CODE) $(TESTS) $(SETUP)
pytest:
	$(PYTEST) $(PYTEST_OPTIONS) $(TESTS)
format:
	black $(CODE) $(TESTS) $(SETUP)
	python3 -misort $(CODE) $(TESTS) $(SETUP)
tagged:
	git describe --tags --exact-match
	test $$(git ls-files -m | wc -l) = 0

untagged:
	if git describe --tags --exact-match ; then exit 1; else exit 0; fi

pypi:   tagged
	if test -f dist/${PACKAGE}-$(shell git describe --tags --exact-match).tar.gz ; then \
			echo "Source package exists."; \
	else \
			python3 -mbuild -snw ; \
	fi
	twine upload \
			dist/${PACKAGE}-$(shell git describe --tags --exact-match).tar.gz \
			dist/$(subst -,_,${PACKAGE})-$(shell git describe --tags --exact-match)-py3-none-any.whl


	
