using BenchmarkDotNet.Running;

namespace Performance.Sandbox
{
    class Program
    {
        static void Main(string[] args)
            => BenchmarkSwitcher.FromAssembly(typeof(Program).Assembly).Run(args);
    }
}
