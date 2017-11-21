﻿namespace Datadog.Trace
{
#if NET45
    using System.Runtime.Remoting.Messaging;

    // TODO:bertrand revisit this when we want to support multiple AppDomains
    internal class AsyncLocalCompat<T>
    {
        private string _name;

        public AsyncLocalCompat(string name)
        {
            _name = name;
        }

        public T Get()
        {
            return (T)CallContext.LogicalGetData(_name);
        }

        public void Set(T value)
        {
            CallContext.LogicalSetData(_name, value);
        }
    }

#else
    using System.Threading;

    internal class AsyncLocalCompat<T>
    {
        private AsyncLocal<T> _asyncLocal = new AsyncLocal<T>();

        public AsyncLocalCompat(string name)
        {
        }

        public T Get()
        {
            return _asyncLocal.Value;
        }

        public void Set(T value)
        {
            _asyncLocal.Value = value;
        }
    }
#endif
}