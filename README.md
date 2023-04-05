## rtool - Redfish for humans

rtool is a purpose-built tool for interacting with Redfish servers.  It holds
the following priorities and goals in this order:

Interoperability, Usability, Performance

### Interoperability
This tool will make every attempt to be as compatible with every Redfish
compliant server as is possible.  It may do this to the detriment of performance
or usability.  In addition, this means that this tool needs to run in every
expected client environment.

### Usability
This tool should be human facing, and make every attempt to abstract away
Redfish transport and protocol specifics, such that users do not need to be a
Redfish expert to use the tool

### Performance
Where possible without conflicting with the previous goals, this tool shall make
every attempt to provide the highest performance possible, given the constraints
of the DMTF Redfish specification.  It will not extend beyond the Redfish
specification in search of performance.
