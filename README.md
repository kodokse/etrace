# etrace

### TL;DR
A simple ETL log viewer. Very early stage. Currently only supports PDB providers. Supports both viewing of log files and live tracing. Can be run with -log <thelog.etl> on the command line or -live <session name>. Log files can be opened and live tracing started from the File-menu. Using the -pdb switch on the command line is optional, PDB files can also be loaded by pressing F5. Main advantage over TraceView is the ability to filter events with regexes, starting with command line arguments and highlight identical rows based on columns, e.g. clicking on the ThreadId column header will color each row based on its thread id.

### Building
First download and build ampp. Then copy ampp_user.props from the ampp/extra/props to the etrace src directory and change the <AMPP_BASE></AMPP_BASE> to <AMPP_BASE>X:\path to\ampp\</AMPP_BASE> (where "X:\path to\ampp\" is the path to ampp. After that you should be able to build.
