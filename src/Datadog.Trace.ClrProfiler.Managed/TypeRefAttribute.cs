using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Datadog.Trace.ClrProfiler
{
    /// <summary>
    /// While cloning types from this assembly into other assemblies (as part of creating bytecode instrumention)
    /// any references to a type with this attribute will be treated as a reference to the identically named
    /// type in the target assembly.
    /// </summary>
    [AttributeUsage(AttributeTargets.Class)]
    public class TypeRefAttribute : Attribute
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="TypeRefAttribute"/> class.
        /// </summary>
        /// <param name="assemblyName">assemblyName</param>
        /// <param name="typeName">typeName</param>
        public TypeRefAttribute(string assemblyName, string typeName)
        {
            AssemblyName = assemblyName;
            TypeName = typeName;
        }

        /// <summary>
        /// Gets or sets the name of the assembly that will declare this type at runtime
        /// </summary>
        public string AssemblyName { get; set; }

        /// <summary>
        /// Gets or sets the name of the type that will be used at runtime
        /// </summary>
        public string TypeName { get; set; }
    }
}
