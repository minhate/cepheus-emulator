.RECIPEPREFIX := >
PY ?= python3
.PHONY: doctor test mockscan status check roadmap syntax ci qemu
doctor:
> ./scripts/doctor.sh
mockscan:
> $(PY) tools/mockscan.py
status:
> $(PY) tools/milestone.py list
check:
> $(PY) tools/milestone.py check
roadmap:
> $(PY) tools/milestone.py roadmap
test:
> $(PY) -m unittest discover -s tests
syntax:
> for f in scripts/*.sh; do bash -n $$f; done
> $(PY) -m py_compile tools/*.py
ci: syntax mockscan check test
qemu:
> ./scripts/setup-qemu.sh && ./scripts/apply-overlay.sh && ./scripts/build-qemu.sh
