using System.Reflection;
using Eclr;
using Iec61131.Engineering.Library;

[assembly: AssemblyTitle("Rant")]
[assembly: AssemblyDescription("Rant for PLCnext: a node, topics and functions from IEC code")]
[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("")]
[assembly: AssemblyCopyright("")]

[assembly: AssemblyVersion("0.1.0.0")]
[assembly: AssemblyFileVersion("0.1.0.0")]

[assembly: NativeDll(@"")]
[assembly: NativeDllFileName("libRant")]
[assembly: NativeDllFlags(NativeDllFlagsAttribute.LibraryModes.DomainAffinityExplicit)]

// No UseDotNetNamespace: IEC code names the types and blocks plainly, Pose and Publisher.
[assembly: Library]
