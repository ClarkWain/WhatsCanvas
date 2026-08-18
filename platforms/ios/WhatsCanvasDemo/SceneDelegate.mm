#import "SceneDelegate.h"

#import "MetalCanvasView.h"

@interface CanvasViewController : UIViewController
@end

@implementation CanvasViewController

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskAll;
}

@end

@implementation SceneDelegate

- (void)scene:(UIScene *)scene
    willConnectToSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)connectionOptions
{
    (void)session;
    (void)connectionOptions;
    if (![scene isKindOfClass:[UIWindowScene class]]) return;

    UIWindowScene *windowScene = (UIWindowScene *)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    CanvasViewController *controller = [[CanvasViewController alloc] init];
    controller.view = [[MetalCanvasView alloc] initWithFrame:self.window.bounds];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
}

@end
