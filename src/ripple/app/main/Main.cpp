#include <cstdint>
#include <string>
static std::uint64_t const SYSTEM_CURRENCY_PARTS = 1000000;

#include <BeastConfig.h>

#include <ripple/basics/Log.h>
//#include <ripple/protocol/digest.h>
//#include <ripple/app/main/Application.h>
#include <ripple/basics/CheckLibraryVersions.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/Sustain.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/TerminateHandler.h>
//#include <ripple/core/TimeKeeper.h>
//#include <ripple/crypto/csprng.h>
//#include <ripple/json/to_string.h>
//#include <ripple/net/RPCCall.h>
//#include <ripple/resource/Fees.h>
//#include <ripple/rpc/RPCHandler.h>
//#include <ripple/protocol/BuildInfo.h>
#include <ripple/beast/clock/basic_seconds_clock.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/beast/utility/Debug.h>

#include <beast/unit_test/dstream.hpp>
#include <beast/unit_test/global_suites.hpp>
#include <beast/unit_test/match.hpp>
#include <beast/unit_test/reporter.hpp>
#include <test/unit_test/multi_runner.h>

//#include <google/protobuf/stubs/common.h>

#include <boost/program_options.hpp>
#include <boost/version.hpp>

#include <cstdlib>
#include <iostream>
#include <utility>
#include <stdexcept>

#ifdef _MSC_VER
#include <sys/types.h>
#include <sys/timeb.h>
#endif

#if BOOST_VERSION >= 106400
#define HAS_BOOST_PROCESS 1
#endif

#if HAS_BOOST_PROCESS
#include <boost/process.hpp>
#endif

namespace po = boost::program_options;
int http_get_main(const char *host, int port, const char *target);
int websocket_main(const char *shost, int nport, const  char *data);
namespace ripple {

//------------------------------------------------------------------------------

static int runUnitTests(
    std::string const& pattern,
    std::string const& argument,
    bool quiet,
    bool log,
    bool child,
    std::size_t num_jobs,
    int argc,
    char** argv)
{
    using namespace beast::unit_test;
    using namespace ripple::test;

#if HAS_BOOST_PROCESS
    if (!child && num_jobs == 1)
#endif
    {
        multi_runner_parent parent_runner;

        multi_runner_child child_runner{num_jobs, quiet, log};
        auto const any_failed = child_runner.run_multi(match_auto(pattern));

        if (any_failed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
#if HAS_BOOST_PROCESS
    if (!child)
    {
        multi_runner_parent parent_runner;
        std::vector<boost::process::child> children;

        std::string const exe_name = argv[0];
        std::vector<std::string> args;
        {
            args.reserve(argc);
            for (int i = 1; i < argc; ++i)
                args.emplace_back(argv[i]);
            args.emplace_back("--unittest-child");
        }

        for (std::size_t i = 0; i < num_jobs; ++i)
            children.emplace_back(boost::process::exe = exe_name, boost::process::args = args);

        int bad_child_exits = 0;
        for(auto& c : children)
        {
            c.wait();
            if (c.exit_code())
                ++bad_child_exits;
        }

        if (parent_runner.any_failed() || bad_child_exits)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
    else
    {
        // child
        multi_runner_child runner{num_jobs, quiet, log};
        auto const anyFailed = runner.run_multi(match_auto(pattern));

        if (anyFailed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
#endif
}

//------------------------------------------------------------------------------

int run (int argc, char** argv)
{
    // Make sure that we have the right OpenSSL and Boost libraries.
    version::checkLibraryVersions();

    using namespace std;

    beast::setCurrentThreadName ("rippled: main");

    po::variables_map vm;

    std::string importText ="";
    {
        importText += "Import an existing node database (specified in the [";
        importText += ConfigSection::importNodeDatabase ();
        importText += "] configuration file section) into the current ";
        importText += "node database (specified in the [";
        importText += ConfigSection::nodeDatabase ();
        importText += "] configuration file section).";
    }

    std::string shardsText = "";
    {
        shardsText += "Validate an existing shard database (specified in the [";
        shardsText += ConfigSection::shardDatabase();
        shardsText += "] configuration file section).";
    }
    // Set up option parsing.
    //
    po::options_description desc ("General Options");
    desc.add_options ()
    ("help,h", "Display this message.")
    ("conf", po::value<std::string> (), "Specify the configuration file.")
    ("rpc", "Perform rpc command (default).")
    ("rpc_ip", po::value <std::string> (), "Specify the IP address for RPC command. Format: <ip-address>[':'<port-number>]")
    ("rpc_port", po::value <std::uint16_t> (), "Specify the port number for RPC command.")
    ("standalone,a", "Run with no peers.")
    ("unittest,u", po::value <std::string> ()->implicit_value (""), "Perform unit tests.")
    ("unittest-arg", po::value <std::string> ()->implicit_value (""), "Supplies argument to unit tests.")
    ("unittest-log", po::value <std::string> ()->implicit_value (""), "Force unit test log output, even in quiet mode.")
#if HAS_BOOST_PROCESS
    ("unittest-jobs", po::value <std::size_t> (), "Number of unittest jobs to run.")
    ("unittest-child", "For internal use only. Run the process as a unit test child process.")
#endif
    ("parameters", po::value< vector<string> > (), "Specify comma separated parameters.")
    ("quiet,q", "Reduce diagnotics.")
    ("quorum", po::value <std::size_t> (), "Override the minimum validation quorum.")
    ("silent", "No output to the console after startup.")
    ("verbose,v", "Verbose logging.")
    ("load", "Load the current ledger from the local DB.")
    ("valid", "Consider the initial ledger a valid network ledger.")
    ("replay","Replay a ledger close.")
    ("ledger", po::value<std::string> (), "Load the specified ledger and start from .")
    ("ledgerfile", po::value<std::string> (), "Load the specified ledger file.")
    ("start", "Start from a fresh Ledger.")
    ("net", "Get the initial ledger from the network.")
    ("debug", "Enable normally suppressed debug logging")
    ("fg", "Run in the foreground.")
    ("import", importText.c_str ())
    ("shards", shardsText.c_str ())
    ("version", "Display the build version.")
    ;

    // Interpret positional arguments as --parameters.
    po::positional_options_description p;
    p.add ("parameters", -1);

    // Parse options, if no error.
    try
    {
        po::store (po::command_line_parser (argc, argv)
            .options (desc)               // Parse options.
            .positional (p)               // Remainder as --parameters.
            .run (),
            vm);
        po::notify (vm);                  // Invoke option notify functions.
    }
    catch (std::exception const&)
    {
        std::cerr << "rippled: Incorrect command line syntax." << std::endl;
        std::cerr << "Use '--help' for a list of options." << std::endl;
        return 1;
    }


    // Run the unit tests if requested.
    // The unit tests will exit the application with an appropriate return code.
    //
    if (vm.count ("unittest"))
    {
        std::string argument;

        if (vm.count("unittest-arg"))
            argument = vm["unittest-arg"].as<std::string>();

        std::size_t numJobs = 1;
        bool unittestChild = false;
#if HAS_BOOST_PROCESS
        if (vm.count("unittest-jobs"))
            numJobs = std::max(numJobs, vm["unittest-jobs"].as<std::size_t>());
        unittestChild = bool (vm.count("unittest-child"));
#endif

        return runUnitTests(
            vm["unittest"].as<std::string>(), argument,
            bool (vm.count ("quiet")),
            bool (vm.count ("unittest-log")),
            unittestChild,
            numJobs,
            argc,
            argv);
    }
    else
    {
#if HAS_BOOST_PROCESS
        if (vm.count("unittest-jobs"))
        {
            // unittest jobs only makes sense with `unittest`
            std::cerr << "rippled: '--unittest-jobs' specified without '--unittest'.\n";
            std::cerr << "To run the unit tests the '--unittest' option must be present.\n";
            return 1;
        }
#endif
    }
}

} // ripple


void ShutdownProtobufLibrary() {
	//
}


// Must be outside the namespace for obvious reasons
//
int main (int argc, char** argv)
{
#ifdef _MSC_VER
    {
        // Work around for https://svn.boost.org/trac/boost/ticket/10657
        // Reported against boost version 1.56.0.  If an application's
        // first call to GetTimeZoneInformation is from a coroutine, an
        // unhandled exception is generated.  A workaround is to call
        // GetTimeZoneInformation at least once before launching any
        // coroutines.  At the time of this writing the _ftime call is
        // used to initialize the timezone information.
        struct _timeb t;
    #ifdef _INC_TIME_INL
            _ftime_s (&t);
    #else
            _ftime (&t);
    #endif
    }
  //??  ripple::sha512_deprecatedMSVCWorkaround();
#endif

#if defined(__GNUC__) && !defined(__clang__)
    auto constexpr gccver = (__GNUC__ * 100 * 100) +
                            (__GNUC_MINOR__ * 100) +
                            __GNUC_PATCHLEVEL__;

    static_assert (gccver >= 50100,
        "GCC version 5.1.0 or later is required to compile rippled.");
#endif

    static_assert (BOOST_VERSION >= 105700, "Boost version 1.57 or later is required to compile rippled");

    //
    // These debug heap calls do nothing in release or non Visual Studio builds.
    //

    // Checks the heap at every allocation and deallocation (slow).
    //
    //beast::Debug::setAlwaysCheckHeap (false);

    // Keeps freed memory blocks and fills them with a guard value.
    //
    //beast::Debug::setHeapDelayedFree (false);

    // At exit, reports all memory blocks which have not been freed.
    //
#if RIPPLE_DUMP_LEAKS_ON_EXIT
    beast::Debug::setHeapReportLeaks (true);
#else
    beast::Debug::setHeapReportLeaks (false);
#endif
	atexit(&ShutdownProtobufLibrary);
	//?? atexit(&google::protobuf::ShutdownProtobufLibrary);

    std::set_terminate(ripple::terminateHandler);
	//return http_get_main("www.baidu.com", 80, "/");
	int result = websocket_main("echo.websocket.org", 80, "Hello, world!");

    //auto const result (ripple::run (argc, argv));

    beast::basic_seconds_clock_main_hook();

    return result;
}



#if _MSC_VER>=1900
#include "stdio.h" 
_ACRTIMP_ALT FILE* __cdecl __acrt_iob_func(unsigned);
#ifdef __cplusplus 
extern "C"
#endif 
FILE* __cdecl __iob_func(unsigned i) {
	return __acrt_iob_func(i);
}
#endif /* _MSC_VER>=1900 */
