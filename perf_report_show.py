import sys


lines = [i.strip().split()[:3]
         for i in open(sys.argv[1])
         if not i.strip().startswith("#") and i.strip()]

kern_sum = sum(float(i[0][:-1]) for i in lines if i[2] == '[kernel.kallsyms]')
user_sum = sum(float(i[0][:-1]) for i in lines if i[2] != '[kernel.kallsyms]')

print "Kernel =", int(kern_sum)
print "User =", int(user_sum)
print "All =", int(kern_sum + user_sum)
