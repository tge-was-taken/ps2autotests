#include <common-ee.h>

void __attribute__((noinline)) test_beq() {
	int result = -1;

	// This is technically not valid MIPS anyway.
	// But it seems to always take target 2?
	asm volatile (
		".set    noreorder\n"

		"li      $8, 0\n"
		"beq     $8, $8, target1_%=\n"
		"beq     $8, $8, target2_%=\n"
		"beq     $8, $8, target3_%=\n"
		"beq     $8, $8, target4_%=\n"
		"nop\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"li      %0, 2\n"
		"j       skip_%=\n"
		"nop\n"

		"target3_%=:\n"
		"li      %0, 3\n"
		"j       skip_%=\n"
		"nop\n"

		"target4_%=:\n"
		"li      %0, 4\n"
		"j       skip_%=\n"
		"nop\n"

		"skip_%=:\n"

		: "+r"(result)
	);

	printf("beq: delay branch: %08x\n", result);
}

void __attribute__((noinline)) test_jal() {
	int result = -1;

	asm volatile (
		".set    noreorder\n"

		"move    $10, $ra\n"
		"li      $8, 0\n"
		"jal     target2_%=\n"
		"li      $ra, 2\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"move    %0, $ra\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("jal: ra order: %08x\n", result);
}

void __attribute__((noinline)) test_jalr_clobber() {
	register int result = -1;

	asm volatile (
		".set noreorder\n"

		"move    $10, $ra\n"
		"la      $8, target2_%=\n"
		"jalr    $8\n"
		"ori     $8, $0, 5\n"
		"nop\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"li      %0, 2\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		".set    reorder\n"
		: "+r"(result) : : "$8", "$10"
	);

	printf("jalr: clobber rs: %08x\n", result);
}

void __attribute__((noinline)) test_jalr() {
	int result = -1;

	asm volatile (
		".set noreorder\n"

		"move    $10, $ra\n"
		"la      $8, target2_%=\n"
		"jalr    $8\n"
		"li      $ra, 2\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"move    %0, $ra\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("jalr: ra order: %08x\n", result);

	result = -1;

	asm volatile (
		".set noreorder\n"

		"move    $10, $ra\n"
		"la      $9, target2_%=\n"
		"jalr    %0, $9\n"
		"nop\n"

		"target1_%=:\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"nop\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("jalr: non-ra: %08x\n", result);

	result = -1;

	asm volatile (
		".set noreorder\n"

		"move    $10, $ra\n"
		"la      $9, target2_%=\n"
		"jalr    %0, $9\n"
		"li      %0, 1\n"

		"target1_%=:\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"nop\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("jalr: non-ra order: %08x\n", result);

	result = -1;

	asm volatile (
		".set noreorder\n"

		"move    $10, $ra\n"
		"la      $8, target2_%=\n"
		".word 0x01004009\n" // jalr $8, $8
		"nop\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"li      %0, 2\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("jalr: rs/rd match: %08x\n", result);

	test_jalr_clobber();
}

void __attribute__((noinline)) test_bltzal() {
	int result = -1;

	asm volatile (
		".set    noreorder\n"

		"move    $10, $ra\n"
		"subu    $8, $0, 1\n"
		"bltzal  $8, target2_%=\n"
		"li      $ra, 2\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"move    %0, $ra\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("bltzal: ra order: %08x\n", result);

	result = -1;

	asm volatile (
		".set    noreorder\n"

		"move    $10, $ra\n"
		"subu    $8, $0, 1\n"
		"bltzall $8, target2_%=\n"
		"li      $ra, 2\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"move    %0, $ra\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("bltzall: ra order: %08x\n", result);
}

void __attribute__((noinline)) test_bgezal() {
	int result = -1;

	asm volatile (
		".set    noreorder\n"

		"move    $10, $ra\n"
		"li      $8, 0\n"
		"bgezal  $8, target2_%=\n"
		"li      $ra, 2\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"move    %0, $ra\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("bgezal: ra order: %08x\n", result);

	result = -1;

	asm volatile (
		".set    noreorder\n"

		"move    $10, $ra\n"
		"li      $8, 0\n"
		"bgezall $8, target2_%=\n"
		"li      $ra, 2\n"

		"target1_%=:\n"
		"li      %0, 1\n"
		"j       skip_%=\n"
		"nop\n"

		"target2_%=:\n"
		"move    %0, $ra\n"

		"skip_%=:\n"
		"move    $ra, $10\n"

		: "+r"(result) : : "$9", "$10"
	);

	printf("bgezall: ra order: %08x\n", result);
}

int main(int argc, char *argv[]) {
	printf("-- TEST BEGIN\n");

	// Let's not bother trying to pass this one.
	test_beq();

	test_jal();
	test_jalr();
	test_bltzal();
	test_bgezal();

	printf("-- TEST END\n");

	return 0;
}
