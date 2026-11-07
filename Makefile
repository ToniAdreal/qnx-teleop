CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

.PHONY: host-test responder clean qnx

host-test:
	python3 -m pytest tests/ -q

responder: qnx/teleop_responder.c qnx/frame.c qnx/frame.h
	$(CC) $(CFLAGS) -Iqnx qnx/teleop_responder.c qnx/frame.c -o responder

# QNX target build. Requires QNX SDP 8.0 (qcc on PATH).
# Get it free for non-commercial use via QNX Everywhere (myQNX account):
# https://www.qnx.com/account/register — then install with QNX Software Center.
qnx:
	qcc -Vgcc_ntox86_64 -O2 -Wall -Wextra -Iqnx qnx/teleop_responder.c qnx/frame.c -o responder-qnx

clean:
	rm -f responder responder-qnx
	rm -rf .pytest_cache tests/__pycache__ src/__pycache__
