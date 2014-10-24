#!/usr/bin/python

#
# fjdriver.py for Fork/Join pool projects
#
# Written by Godmar and Scott
# Fall 2014, CS 3214
#
version = "$Id: fjdriver.py,v 1.21 2014/10/20 15:25:14 cs3214 Exp $"

#
import getopt, sys, os, subprocess, signal, re, json, resource, time
from datetime import datetime
from collections import namedtuple, defaultdict

src_dir = "/home/courses/cs3214/threadlab/threadlab-fall2014"
results_file = "full-results.json"
filelist = src_dir + "/FILELIST"
timestamp = str(datetime.now()).replace(" ", "_")
workdir = "fj_testdir_" + timestamp
poolfile = "./threadpool.c"
verbose = False
silent = False
list_tests = False

# Test definitions

thread_count = [1, 2, 4, 8, 16]
__threadpool_test = namedtuple('threadpool_test',
                             ['name',           # name of the test (quicksort, fib, etc)
                              'command',        # command line to execute
                              'description',    # description of test
                              'is_required',     # is this test part of the minimum requirements?
                              'runs'])

def threadpool_test(name, command, description, runs, is_required=False):
    return __threadpool_test(name, command, description, is_required, runs)

__test_run = namedtuple('test_run', [
    'name', 'args', 'thread_count', 'input_file', 'is_benchmarked', 'timeout'
])

def test_run(name, args, thread_count=thread_count, input_file=None, is_benchmarked=False, timeout=5):
    return __test_run(name, args, thread_count, input_file, is_benchmarked, timeout)

##########################################################################33
seed=str(42)
large_sort_size = 150000000
medium_sort_size = large_sort_size/10
small_sort_size = large_sort_size/100

tests = [
    threadpool_test(
        name="basic1",
        command="./threadpool_test",
        description="Basic functionality testing (1)",
        is_required = True,
        runs=[
            test_run(name="basic test 1", args=[], thread_count=[1,2,4])
        ]
    ),
    threadpool_test(
        name="basic2",
        command="./threadpool_test2",
        description="Basic functionality testing (2)",
        is_required = True,
        runs=[
            test_run(name="basic test 2", args=[], thread_count=[1,2,4])
        ]
    ),
    threadpool_test(
        name="basic3",
        command="./threadpool_test3",
        description="Basic functionality testing (3)",
        is_required = True,
        runs=[
            test_run(name="basic test 3", args=[], thread_count=[1,2,4])
        ]
    ),
    threadpool_test(
        name="mergesort",
        command="./mergesort",
        description="parallel mergesort",
        runs=[
            test_run(name="mergesort small", args=["-s", seed, str(small_sort_size)]),
            test_run(name="mergesort medium", args=["-s", seed, str(medium_sort_size)]),
            test_run(name="mergesort large", args=["-s", seed, str(large_sort_size)], 
                thread_count=[4,8,16], is_benchmarked=True, timeout=8),
        ]
    ),
    threadpool_test(
        name="quicksort",
        command="./quicksort",
        description="parallel quicksort",
        runs=[
            test_run(name="quicksort small", args=["-s", seed, "-d", "16", str(small_sort_size)]),
            test_run(name="quicksort medium", args=["-s", seed, "-d", "16", str(medium_sort_size)]),
            test_run(name="quicksort large", args=["-s", seed, "-d", "16", str(large_sort_size)], 
                thread_count=[4,8,16], is_benchmarked=True, timeout=8),
        ]
    ),
    threadpool_test(
        name="psum",
        command="./psum_test",
        description="parallel sum using divide-and-conquer",
        runs=[
            test_run(name="psum_test small", args=["10000000"]),
            test_run(name="psum_test medium", args=["100000000"]),
            test_run(name="psum_test large", args=["1000000000"], thread_count=[4,8,16], timeout=8),
        ]
    ),
    threadpool_test(
        name="nqueens",
        command="./nqueens",
        description="parallel n-queens solver",
        runs=[
            test_run(name="nqueens 11", args=["11"]),
            test_run(name="nqueens 12", args=["12"], timeout=8),
            test_run(name="nqueens 13", args=["13"], thread_count=[4,8,16], 
                is_benchmarked=True, timeout=8),
        ]
    ),
    threadpool_test(
        name="fibonacci",
        command="./fib_test",
        description="parallel fibonacci toy test",
        runs=[
            test_run(name="fibonacci 30", args=["30"], timeout=8),
            # test_run(name="fibonacci_12", args=["36"], thread_count=[1,2,4]),
            test_run(name="fibonacci 38", args=["38"], thread_count=[1,2,4], timeout=8),
        ]
    ),
]

#
# getopt
#
# look for threadpool.c in current dir, or point at with flag
#
def usage():
    print """
Usage: %s [options]
    -v              Verbose
    -V              Print Version and exit
    -a              Run benchmark anyway even if machine is not idle
    -r              Only run required tests.
    -h              Show help 
    -p              <file> - Location of threadpool implementation, default ./threadpool.c
    -l              List available tests
    -t              Filter test by name, given as a comma separated list.
                    e.g.: -t basic1,psum
    """ % (sys.argv[0])

try:
    opts, args = getopt.getopt(sys.argv[1:], "Varvhlp:t:", ["verbose", "help", "list-tests"])
except getopt.GetoptError, err:
    print str(err) # will print something like "option -a not recognized"
    usage()
    sys.exit(2)

runfilter = lambda test : True
ignore_if_not_idle = False

for opt, arg in opts: 
    if opt == "-r":
        oldrunfilter = runfilter
        runfilter = lambda test: test.is_required and oldrunfilter(test)

    elif opt == "-V":
        print "Version", version
        sys.exit(0)
    elif opt == "-a":
        ignore_if_not_idle = True
    elif opt == "-v":
        verbose = True
    elif opt in ("-h", "--help"):
        usage()
        sys.exit()
    elif opt == '-p':
        poolfile = arg
    elif opt == '-l':
        list_tests = True
    elif opt == '-t':
        filtered = arg.split(',')
        for filter in filtered:
            for test in tests:
                if filter == test.name:
                    break
            else:
                print 'Unknown test: %s. Use -l to list test names.' % filter
                usage()
                sys.exit()
        oldrunfilter = runfilter
        runfilter = lambda test: test.name in filtered and oldrunfilter(test)
    else:
        assert False, "unhandled option"

if list_tests:
    print 'Available tests (with applied filters):'
    print 80 * '='
    for test in tests:
        if runfilter(test):
            print '%s: %s' % (test.name, test.description)
    sys.exit()

def copyfile(src, dst):
    cmd = "cp %s %s" % (src, dst)
    if verbose:
        print cmd
    ex = os.system(cmd)
    if ex:
        sys.exit(ex)

def setup_working_directory():
    if verbose:
        print "Creating working directory",  workdir

    os.mkdir(workdir)

    if verbose:
        print "Copying files"

    if not os.access(poolfile, os.R_OK):
        print
        print "I cannot find %s" % poolfile
        usage()
        sys.exit(2)

    copyfile(poolfile, workdir + "/threadpool.c")

    flist = open(filelist, 'r')
    for file in flist:
        if file.startswith("#"):
            continue
        file = file.strip()
        copyfile(src_dir + "/" + file, workdir)

    flist.close()
    if verbose:
        print "Copying %s" % poolfile

    os.chdir(workdir)

    if os.system("make"):
        raise Exception("make failed, run 'make' in %s to see why" % workdir)

def check_software_engineering(objfile, allowedsymbols):
    hex = "[0-9A-Fa-f]{8,16}"
    if verbose:
        print "Performing some checks that %s conforms to accepted software engineering practice..." % objfile

    symbols = subprocess.Popen(["nm", objfile], stdout=subprocess.PIPE)\
        .communicate()[0].split("\n")

    for sym in symbols:
        if sym == "" or re.match("\s+U (\S+)", sym):
            continue

        m = re.match(hex + " (\S) (\S+)", sym)
        if not m:
            raise Exception("unexpected line in nm:\n" + sym)

        if m.group(1).islower():    # local symbols are fine
            continue

        if m.group(1) == "T":
            if m.group(2) in allowedsymbols:
                continue

            raise Exception(("%s defines global function '%s'\n"
                +"allowed functions are: %s") % (objfile, m.group(2), str(allowedsymbols)))

        raise Exception(("%s must not define any global or static variables"
                +", but you define: %s") % (objfile, m.group(2)))

allowedsymbols = [ "future_free", "future_get",
                   "thread_pool_new", "thread_pool_shutdown_and_destroy", 
                   "thread_pool_submit" ] 

#
# build it (like check.py)
#

def count_number_of_processes():
    proc = subprocess.Popen(["ps", "ux"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    stdout, stderr = proc.communicate()
    # -2 for ps and header
    return len(stdout.strip().split('\n')) - 2

def get_load_average():
    """
    Returns tuple nproc, loadavg where nproc is the current number of
    running threads (minus 1) and loadavg is the load average
    """
    # 0.57 0.65 0.54 1/426 28121
    f = open("/proc/loadavg")
    c = f.read().strip()
    m = re.match(r'(\S+) \S+ \S+ (\d+)/\d+ \d+', c)
    load = float(m.groups()[0])
    nprocs = int(m.groups()[1])
    f.close()
    return nprocs - 1, load

def wait_for_load_to_go_down():
    while True:
        nprocs, load = get_load_average()
        if nprocs == 0 and load < 1.0:
            break

        print "Warning. There are other %d processes running on this machine, loadavg %f." % (nprocs, load)
        print "Sleeping for 1 second.  Use the -a switch to run the benchmarks regardless."
        time.sleep(1.0)

# run tests
#
# echo test:
#  progname  + set of inputs/cmdline parameters
#
# each should support -n <thread> for number of threads
#

#
# result: expected output + exit code(?)
#

def set_threadlimit(nthreads):
    def closure():
        resource.setrlimit(resource.RLIMIT_NPROC, (nthreads, nthreads))
    return closure
    
def run_tests(tests):
    results = defaultdict(dict)

    for test in tests:
        if not runfilter(test):
            if verbose:
                print 'Skipping test: ' + test.description
            continue

        if not silent:
            print ''
            print 'Starting test: ' + test.description
            print '=' * 80

        results[test.name] = { }
        for run in test.runs:
            perthreadresults = []
            results[test.name][run.name] = perthreadresults

            for threads in run.thread_count:
                cmdline = ['timeout', str(run.timeout), test.command, '-n', str(threads) ] + run.args
                rundata = {
                    'command' : ' '.join(cmdline),
                    'nthreads' : threads
                }
                def addrundata(d):
                    for k, v in d.items():
                        rundata[k] = v

                if not silent:
                    print 'Running:', ' '.join(cmdline),
                    sys.stdout.flush()
                infile = None
                if run.input_file:
                    infile = open(run.input_file, 'r')
                # preexec_fn sets the system-wide NPROC for this user.
                # we set it to #threads + 1 (for the main thread)
                # plus existing procs
                starttime = time.time()
                proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, stdin=infile,
                                        preexec_fn=set_threadlimit(threads + 2 + number_of_existing_processes))

                stdout, stderr = proc.communicate()
                runningtime = time.time() - starttime

                if infile:
                    infile.close()

                if proc.returncode >= 128:
                    signames = dict((k, v) for v, k in signal.__dict__.iteritems() if v.startswith('SIG'))
                    signum = proc.returncode - 128
                    timeoutmsg = ''
                    if runningtime >= run.timeout:
                        timeoutmsg = '\nProgram ran for %.3fs and most likely timed out at %.3fs' % (runningtime, run.timeout)
                    error = """\
                    Program terminated with signal %d (%s) %s
                    --------------------------------------------
                    Program output:
                    %s
                    StdErr output:
                    %s
                    """ % (signum, signames[signum], timeoutmsg, stdout, stderr)
                    addrundata({
                        'error': error
                    })
                    if not silent:
                        print '[ ]'
                        if verbose:
                            print error

                elif proc.returncode > 0:
                    # non-zero exit code
                    timeoutmsg = ''
                    if proc.returncode == 124:
                        timeoutmsg = '\nProgram ran for %.3fs and most likely timed out at %.3fs' % (runningtime, run.timeout)
                    
                    error = """\
                    Program exited with non-zero exit status %d. %s
                    --------------------------------------------
                    Program output:
                    %s
                    StdErr output:
                    %s
                    """ % (proc.returncode, timeoutmsg, stdout, stderr)

                    addrundata({
                        'error': error
                    })
                    if not silent:
                        print '[ ]'
                        if verbose:
                            print error

                else:
                    if not silent:
                        print '[+]'

                    outfile = 'runresult.%d.json' % (proc.pid)
                    if not os.access(outfile, os.R_OK):
                        addrundata({
                            'error': 'The benchmark did not create the expected result file %s' % outfile
                        })
                    else:
                        f = open(outfile, 'r')
                        data = f.read()
                        f.close()
                        addrundata(json.loads(data))
                        rundata['stdout'] = stdout
                        if len(stderr) > 0: 
                            rundata['stderr'] = stderr
                        os.unlink(outfile)

                perthreadresults.append(rundata)
    return results

def print_results(results):
    print json.dumps(results, indent = 4, sort_keys = True, separators = (',', ': '))

def write_results_to_json(filename):
    jfile = open(results_file, "w")
    print >>jfile, json.dumps(results, indent = 4, sort_keys = True, separators = (',', ': '))
    jfile.close()

def find_thread_run(perthreadresults, threadcount):
    for result in perthreadresults:
        if result['nthreads'] == threadcount:
            return result
    return None

def print_grade_table(results, tests):
    thread_headers = [1,2,4,8,16]
    print ''
    print 'Test name:' + (16 * ' ') + ''.join(map(lambda x: '%-10d' % x, thread_headers))
    print '='*80
    minimum_requirements = True
    for test in tests:
        if not runfilter(test) and test.is_required:
            if not silent:
                print 'WARNING: Skipping minimum requirement test (%s), will not say you passed!' % test.name
            minimum_requirements = False

        if not runfilter(test):
            continue
        if not test.name in results:
            print '%s: could not find test data!' % test.name
        res = results[test.name]
        print '%s:' % test.name.upper() + '  ' + test.description
    
        passed = True
        for run in test.runs:
            statuses = []
            for threads in thread_headers:
                thread_run = find_thread_run(res[run.name], threads)
                if not thread_run:
                    statuses.append('')
                elif 'error' in thread_run:
                    passed = False
                    statuses.append('[ ]')
                elif run.is_benchmarked:
                    statuses.append('[%.3fs]' % thread_run['realtime'])
                    pass
                else:
                    statuses.append('[X]')

            print '  %-23s' % (run.name) + ''.join(map(lambda x: '%-10s' % x, statuses))
        
        if not passed and test.is_required:
            minimum_requirements = False

    print '='*80
    if minimum_requirements:
        print 'You have met minimum requirements.'
    else:
        print 'You did not meet minimum requirements.'


setup_working_directory()
check_software_engineering("threadpool.o", allowedsymbols)
number_of_existing_processes = count_number_of_processes()
if verbose:
    print "There are %d process currently running for user %s" % (number_of_existing_processes, os.getlogin())

if not ignore_if_not_idle:
    wait_for_load_to_go_down()
    
results = run_tests(tests)
if verbose:
    print_results(results)
if not silent:
    print_grade_table(results, tests)

write_results_to_json(results_file)
print "Wrote full results to %s/%s" % (workdir, results_file)

