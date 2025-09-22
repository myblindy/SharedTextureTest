using Silk.NET.Core.Native;
using Silk.NET.Direct3D11;
using Silk.NET.DXGI;
using System.Diagnostics;
using System.IO;
using System.IO.Pipelines;
using System.IO.Pipes;
using System.Windows.Shapes;

namespace SharedTextureTestClient;

class SharedTextureInterop
{
    readonly D3D11 d3d11 = D3D11.GetApi();
    readonly ComPtr<ID3D11Device5> d3d11Device;
    readonly ComPtr<ID3D11DeviceContext> d3d11DeviceContext;
    readonly ComPtr<ID3D11Texture2D> sharedTexture;

    public unsafe SharedTextureInterop()
    {
        Debugger.Launch();

        IntPtr sharedTextureMemoryHandle;
        int width, height;
        using (var pipe = new NamedPipeClientStream(".", "SharedTextureTestPipe", PipeDirection.InOut, System.IO.Pipes.PipeOptions.Asynchronous))
        using (var pipeReader = new StreamReader(pipe))
        {
            pipe.Connect();

            string? line;
            do
            {
                line = pipeReader.ReadLine();
            } while (line is null);
            var sharedInfo = line.Split(' ');
            (sharedTextureMemoryHandle, width, height) = (IntPtr.Parse(sharedInfo[0]), int.Parse(sharedInfo[1]), int.Parse(sharedInfo[2]));
            pipe.Write([0]);
        }

        // d3d11 device
        ReadOnlySpan<D3DFeatureLevel> featureLevels = [D3DFeatureLevel.Level111, D3DFeatureLevel.Level110];
        fixed (D3DFeatureLevel* pFeatureLevels = featureLevels)
            SilkMarshal.ThrowHResult(d3d11.CreateDevice(default(ComPtr<IDXGIAdapter>), D3DDriverType.Hardware, 0, (uint)CreateDeviceFlag.BgraSupport,
                pFeatureLevels, 2, D3D11.SdkVersion, ref d3d11Device, null, ref d3d11DeviceContext));

        // d3d11 shared texture
        SilkMarshal.ThrowHResult(d3d11Device.OpenSharedResource1((void*)sharedTextureMemoryHandle, out sharedTexture));
    }
}
