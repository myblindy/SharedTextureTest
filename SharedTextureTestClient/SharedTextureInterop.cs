using Silk.NET.Core.Native;
using Silk.NET.Direct3D11;
using Silk.NET.DXGI;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using Windows.Win32;
using Windows.Win32.Foundation;

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

        HANDLE sourceProcess, sharedTextureMemoryHandle;
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
            (sourceProcess, sharedTextureMemoryHandle, width, height) =
                (new(IntPtr.Parse(sharedInfo[0])), new(IntPtr.Parse(sharedInfo[1])), int.Parse(sharedInfo[2]), int.Parse(sharedInfo[3]));
            pipe.Write([0]);
        }

        HANDLE localSharedTextureMemoryHandle;
        PInvoke.DuplicateHandle(sourceProcess, sharedTextureMemoryHandle, new(Process.GetCurrentProcess().Handle), &localSharedTextureMemoryHandle,
            0, false, DUPLICATE_HANDLE_OPTIONS.DUPLICATE_SAME_ACCESS);

        // d3d11 device
        ReadOnlySpan<D3DFeatureLevel> featureLevels = [D3DFeatureLevel.Level111, D3DFeatureLevel.Level110];
        fixed (D3DFeatureLevel* pFeatureLevels = featureLevels)
            SilkMarshal.ThrowHResult(d3d11.CreateDevice(default(ComPtr<IDXGIAdapter>), D3DDriverType.Hardware, 0, (uint)CreateDeviceFlag.BgraSupport,
                pFeatureLevels, 2, D3D11.SdkVersion, ref d3d11Device, null, ref d3d11DeviceContext));

        // d3d11 shared texture
        //ComPtr<IDXGISurface> dxgiSurface;
        SilkMarshal.ThrowHResult(d3d11Device.OpenSharedResource1((void*)localSharedTextureMemoryHandle, out sharedTexture));
    }
}
