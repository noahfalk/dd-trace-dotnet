using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Datadog.Trace.ClrProfiler.Managed.Loader
{
    /* This code won't run from this assembly. The native profiler will copy the
     * definition of this type into the first non-corlib assembly to JIT a method
     * and then it will modify the method being jitted to prepend a call to
     * DDVoidMethodType.DDVoidMethodCall()
     */
    internal static class DDVoidMethodType
    {
        internal static void DDVoidMethodCall()
        {
            GetAssemblyAndSymbolsBytes(out IntPtr assemblyPtr, out int assemblySize, out IntPtr symbolsPtr, out int symbolsSize);
            byte[] assemblyBytes = new byte[assemblySize];
            Marshal.Copy(assemblyPtr, assemblyBytes, 0, assemblySize);
            byte[] symbolBytes = new byte[symbolsSize];
            Marshal.Copy(symbolsPtr, symbolBytes, 0, symbolsSize);
            Assembly loadedAssembly = System.AppDomain.CurrentDomain.Load(assemblyBytes, symbolBytes);
            loadedAssembly.CreateInstance("Datadog.Trace.ClrProfiler.Managed.Loader.Startup");
        }

        [DllImport("DATADOG.TRACE.CLRPROFILER.NATIVE.DLL")]
        private static extern void GetAssemblyAndSymbolsBytes(out IntPtr assemblyPtr, out int assemblySize, out IntPtr symbolsPtr, out int symbolsSize);
    }
}
