docker-build:
	docker build -t="combos" .

docker-rerun:
	docker rm -f combos || true
	docker run --name=combos -v "$(CURDIR)/experiments:/app/experiments" combos

docker-inspect:
	docker rm -f combos || true
	docker run -it --name=combos -v "$(CURDIR)/experiments:/app/experiments" combos //bin/bash
