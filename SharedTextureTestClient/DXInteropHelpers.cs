using Silk.NET.Core.Native;
using Silk.NET.Direct2D;

namespace SharedTextureTestClient;

static class DXInteropHelpers
{
    public static unsafe int D2D1CreateFactory(this D2D d2d, FactoryType factoryType, ref ComPtr<ID2D1Factory> factory)
    {
        var riid = ID2D1Factory.Guid;
        FactoryOptions factoryOptions = new();
        ID2D1Factory* factoryPointer = null;

        var result = d2d.D2D1CreateFactory(factoryType, &riid, &factoryOptions, (void**)&factoryPointer);
        factory = new(factoryPointer);
        return result;
    }
}
