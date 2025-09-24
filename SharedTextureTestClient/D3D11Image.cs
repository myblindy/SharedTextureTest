using Silk.NET.Core.Native;
using Silk.NET.Direct3D11;
using Silk.NET.Direct3D9;
using Silk.NET.DXGI;
using System.CodeDom;
using System.Windows.Interop;
using Windows.Win32;

namespace SharedTextureTestClient;

internal class D3D11Image : D3DImage, IDisposable
{
    ComPtr<IDirect3DTexture9>? backbuffer;

    readonly Lock d3d9Lock = new();
    readonly D3D9 d3d9 = D3D9.GetApi();
    readonly ComPtr<IDirect3D9Ex> d3d9Ex;
    readonly ComPtr<IDirect3DDevice9Ex> d3d9DeviceEx;

    public unsafe D3D11Image()
    {
        lock (d3d9Lock)
        {
            SilkMarshal.ThrowHResult(d3d9.Direct3DCreate9Ex(D3D9.SdkVersion, ref d3d9Ex));

            var presentParams = new Silk.NET.Direct3D9.PresentParameters
            {
                Windowed = true,
                SwapEffect = Swapeffect.Discard,
                BackBufferFormat = Silk.NET.Direct3D9.Format.Unknown,
                PresentationInterval = D3D9.PresentIntervalDefault,
                BackBufferHeight = 1,
                BackBufferWidth = 1,
                HDeviceWindow = PInvoke.GetDesktopWindow()
            };
            SilkMarshal.ThrowHResult(d3d9Ex.CreateDeviceEx(0, Devtype.Hal, default,
                D3D9.CreateHardwareVertexprocessing | D3D9.CreateMultithreaded | D3D9.CreateFpuPreserve,
                ref presentParams, null, ref d3d9DeviceEx));
        }
    }

    unsafe ComPtr<IDirect3DTexture9> GetSharedTexture(ComPtr<ID3D11Texture2D> d3d11RenderTarget)
    {
        SharedResource sharedHandle;
        using var dxgiTextureResource = d3d11RenderTarget.QueryInterface<IDXGIResource1>();
        SilkMarshal.ThrowHResult(dxgiTextureResource.CreateSharedHandle(default(SecurityAttributes*),
            DXGI.SharedResourceRead, default(char*), (void**)&sharedHandle));

        Texture2DDesc desc = default;
        d3d11RenderTarget.GetDesc(ref desc);

        ComPtr<IDirect3DTexture9> d3d9Texture = default;
        SilkMarshal.ThrowHResult(d3d9DeviceEx.CreateTexture(desc.Width, desc.Height, 1, D3D9.UsageRendertarget, desc.Format switch
        {
            Silk.NET.DXGI.Format.FormatB8G8R8X8Unorm => Silk.NET.Direct3D9.Format.X8R8G8B8,
            Silk.NET.DXGI.Format.FormatB8G8R8A8Unorm => Silk.NET.Direct3D9.Format.A8R8G8B8,
            _ => throw new NotSupportedException($"Unsupported DXGI format {desc.Format}")
        }, Pool.Default, ref d3d9Texture, ref sharedHandle.Handle));

        return d3d9Texture;
    }

    public unsafe void SetBackBuffer(ComPtr<ID3D11Texture2D>? d3d11RenderTarget)
    {
        var previousBackBuffer = backbuffer;

        if (d3d11RenderTarget.HasValue)
        {
            backbuffer = GetSharedTexture(d3d11RenderTarget.Value);

            ComPtr<IDirect3DSurface9> dxgiSurface = default;
            try
            {
                SilkMarshal.ThrowHResult(backbuffer.Value.GetSurfaceLevel(0, ref dxgiSurface));

                Lock();
                SetBackBuffer(D3DResourceType.IDirect3DSurface9, new(dxgiSurface.Handle));
                Unlock();
            }
            finally { dxgiSurface.Dispose(); }
        }
        else
        {
            backbuffer = null;

            Lock();
            SetBackBuffer(D3DResourceType.IDirect3DSurface9, IntPtr.Zero);
            Unlock();
        }

        previousBackBuffer?.Dispose();
    }

    #region IDisposable
    private bool disposedValue;

    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                // managed state 
                SetBackBuffer(null);
                backbuffer?.Dispose();
                d3d9DeviceEx.Dispose();
                d3d9Ex.Dispose();
                d3d9.Dispose();
            }

            // unmanaged resources 
            disposedValue = true;
        }
    }

    ~D3D11Image()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        Dispose(disposing: false);
    }

    public void Dispose()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }
    #endregion
}
