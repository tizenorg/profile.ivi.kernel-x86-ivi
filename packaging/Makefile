
all: adaptation-intel-automotive 

adaptation-intel-automotive: kernel.spec.in series makespec.pl
	@touch IVI;
	@perl makespec.pl < kernel.spec.in > kernel-adaptation-intel-automotive.spec;
	@rm IVI;
