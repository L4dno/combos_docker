docker-build:
	docker build -t="combos" .

docker-rerun:
	docker rm -f combos || true
	docker run --name=combos -v "$(CURDIR)/analysis:/app/analysis" combos

run:
	docker exec -it combos /app/experiments/script.sh

docker-inspect:
	docker rm -f combos || true
	docker run -it --name=combos -v "$(CURDIR)/analysis:/app/analysis" combos //bin/bash
