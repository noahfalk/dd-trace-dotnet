using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Datadog.Trace.ClrProfiler
{
    /// <summary>
    /// This might be combinable with the InterceptMethod but I named it separately for the moment
    /// so I wouldn't need to worry about integration processing
    /// </summary>
    [AttributeUsage(AttributeTargets.Method)]
    internal class InterceptAttribute : Attribute
    {
        public InterceptAttribute(string interceptedMethodName)
        {
            InterceptedMethodName = interceptedMethodName;
        }

        public string InterceptedMethodName { get; set; }
    }
}
