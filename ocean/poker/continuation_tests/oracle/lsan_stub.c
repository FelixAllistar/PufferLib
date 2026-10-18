/* Test executable only. Address and undefined-behavior instrumentation remain
 * active. The sandbox cannot run LeakSanitizer's process-inspection exit pass. */
int __lsan_is_turned_off(void) { return 1; }
