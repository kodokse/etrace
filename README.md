# etrace

### TL;DR
A simple ETL log viewer. Very early stage. Currently only supports viewing of log files, no live tracing yet. Must be run with -log <thelog.etl> on the command line. Using -pdb on the command line is optional as PDB files can be loaded by pressing F5.

### Building
First download and build ampp. Then copy ampp_user.props from the ampp/extra/props to the etrace src directory and change the <AMPP_BASE></AMPP_BASE> to <AMPP_BASE>X:\path to\ampp\</AMPP_BASE> (where "X:\path to\ampp\" is the path to ampp. After that you should be able to build.
