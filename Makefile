docker-build:
	docker build -t="combos" .

docker-rerun:
	docker rm -f combos || true
	docker run --name=combos -v "$(CURDIR)/experiments:/app/experiments" combos

run:
	docker exec -it combos /app/experiments/script.sh