// Copied verbatim from essej/paulxstretch @ b6175e9, Source/PluginProcessor.cpp lines 18-48 (GPL-3; Xenakios, Jesse Chappell):
// the plug-in's FFT size rounding, which lives in its processor file, not in PS_Source.
int get_optimized_updown(int n, bool up) {
	int orig_n = n;
	while (true) {
		n = orig_n;

#if PS_USE_VDSP_FFT
        // only powers of two allowed if using VDSP FFT
#elif PS_USE_PFFFT
        // only powers of two allowed if using pffft
#else
        while (!(n % 11)) n /= 11;
		while (!(n % 7)) n /= 7;
		while (!(n % 5)) n /= 5;
		while (!(n % 3)) n /= 3;
#endif

        while (!(n % 2)) n /= 2;
		if (n<2) break;
		if (up) orig_n++;
		else orig_n--;
		if (orig_n<4) return 4;
	};
	return orig_n;
};

int optimizebufsize(int n) {
	int n1 = get_optimized_updown(n, false);
	int n2 = get_optimized_updown(n, true);
	if ((n - n1)<(n2 - n)) return n1;
	else return n2;
};
