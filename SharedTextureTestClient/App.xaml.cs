using Evergine.Bindings.RenderDoc;
using System.Windows;

namespace SharedTextureTestClient;

public partial class App : Application
{
    public static RenderDoc RenderDoc { get; private set; } = null!;

    public App()
    {
        RenderDoc.Load(out var api);
        RenderDoc = api;
    }
}
