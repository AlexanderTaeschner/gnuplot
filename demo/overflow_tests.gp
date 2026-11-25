#
# Various tests of integer arithmetic overflow
#
print ""
print "======================================================="
print "Start with various test of handling arithmetic overflow"
print "No problems are expected with any version of gnuplot 6."
print "======================================================="
A = 2**62
print "A = 2**62"

unset overflow
print ""
print "unset overflow"
print "print A, A+A, -3*A"
print A, A+A, -3*A

set overflow
print ""
print "set overflow"
print "print A, A+A, -3*A"
print A, A+A, -3*A

set overflow NaN
print ""
print "set overflow NaN"
print "print A, A+A, -3*A"
print A, A+A, -3*A

print ""
print "==========================================="
print " difficult overflow detection for products "
print "==========================================="

print "set overflow"; set overflow; show overflow
B = (2**62-1) << 1
print "B = (2**62-1) << 1"
print "print B, B+1, B+2, B+3"
print B, B+1, B+2, B+3

print ""
print "B/3 = ", B/3
print "print 3074457345618258602 * 3 :  ", 3074457345618258602 * 3, "   Should be OK"
print "print 3074457345618258603 * 3 :  ", 3074457345618258603 * 3, "   Should overflow"

print ""
print "print (B-510), int(real(B-510))","\t", (B-510), int(real(B-510))
print "print (B-511), int(real(B-511))","\t", (B-511), int(real(B-511))
print ""
print "So any product > 9.22337203685477478e+18 "
print "may or may not have overflowed and needs additional tests"


#
# Test overflow reporting for INT64 arithmetic
#
print ""
print "==========================================================="
print "The following tests probe cases where overflow handling"
print "failed in earlier versions of gnuplot 6."
print "Some were a consequence of optimizations added to clang v21."
print "==========================================================="

BIG       = 0x5000000000000000
INT64_MAX = 0x7fffffffffffffff

set overflow float
print "\n## set overflow float"

# f_power
print "\nTesting f_power"
print "2**62 =     ", 2**62
print "2**63 =     ", 2**63
print "2**100 =    ", 2**100

# f_mult
print "\nTesting f_mult"
print "BIG =       ", "0x5000000000000000"
print "BIG * 2 =   ", BIG*2
print "BIG * BIG = ", BIG*BIG

# f_plus
print "\nTesting f_plus"
print "INT64_MAX =      ", INT64_MAX
print "INT64_MAX + 1 =  ", INT64_MAX + 1
print "BIG + BIG     =  ", BIG + BIG

# f_prod
print "\nTesting f_prod"
N = 2**21 - 1
print "N = 2**21 - 1"
print "prod [i=1:3] N =      ", prod [i=1:3] N
print "N*N*N =               ", N*N*N
print "prod [i=1:4] N =      ", prod [i=1:4] N
print "N*N*N*N =             ", N*N*N*N

# f_sum
print "\nTesting f_sum"
print "sum [i=1:2] BIG = ", sum [i=1:2] BIG
print "sum [i=1:3] BIG = ", sum [i=1:3] BIG

#
print "\nTesting cases of int64->double loss of precision"
N = 2**53
M = N + 1
print "N = 2**53"
print "M = N + 1"
print "print  N, M:   ", N, M
print "print  M - N:  ", M - N
print "print -N + M:  ", -N + M


