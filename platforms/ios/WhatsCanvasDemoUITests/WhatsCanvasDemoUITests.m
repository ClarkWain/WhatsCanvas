#import <XCTest/XCTest.h>

@interface WhatsCanvasDemoUITests : XCTestCase
@end

@implementation WhatsCanvasDemoUITests

- (void)setUp
{
    self.continueAfterFailure = NO;
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationPortrait;
}

- (void)tearDown
{
    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationPortrait;
}

- (void)testPortraitLandscapeBackgroundAndColdLaunch
{
    XCUIApplication *app = [[XCUIApplication alloc] init];
    app.launchArguments = @[@"--capture-time=1.25"];
    app.launchEnvironment = @{
        @"MTL_DEBUG_LAYER": @"1",
        @"MTL_DEBUG_ERROR_MODE": @"0",
    };
    [app launch];
    XCTAssertTrue([app waitForState:XCUIApplicationStateRunningForeground
                            timeout:10.0]);
    XCTAssertTrue([self waitForCanvasReady:app]);

    XCTAttachment *portrait = [XCTAttachment attachmentWithScreenshot:
        XCUIScreen.mainScreen.screenshot];
    portrait.name = @"Portrait";
    portrait.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:portrait];

    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCTAssertTrue([self waitForLandscapeScreen]);
    XCTAssertTrue([self waitForCanvasReady:app]);
    XCTAttachment *landscape = [XCTAttachment attachmentWithScreenshot:
        XCUIScreen.mainScreen.screenshot];
    landscape.name = @"Landscape";
    landscape.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:landscape];

    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeRight;
    XCTAssertTrue([self waitForLandscapeScreen]);
    XCTAssertTrue([self waitForCanvasReady:app]);

    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationPortrait;
    XCTAssertTrue([self waitForPortraitScreen]);
    XCTAssertTrue([self waitForCanvasReady:app]);

    for (NSUInteger cycle = 0; cycle < 3; ++cycle) {
        [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];
        XCTAssertTrue([app waitForState:XCUIApplicationStateRunningBackground
                                timeout:5.0]);
        [app activate];
        XCTAssertTrue([app waitForState:XCUIApplicationStateRunningForeground
                                timeout:10.0]);
        XCTAssertTrue([self waitForCanvasReady:app]);
    }

    for (NSUInteger launch = 0; launch < 2; ++launch) {
        [app terminate];
        XCTAssertTrue([app waitForState:XCUIApplicationStateNotRunning
                                timeout:5.0]);
        [app launch];
        XCTAssertTrue([app waitForState:XCUIApplicationStateRunningForeground
                                timeout:10.0]);
        XCTAssertTrue([self waitForCanvasReady:app]);
    }
}

- (BOOL)waitForCanvasReady:(XCUIApplication *)app
{
    XCUIElement *canvas = app.otherElements[@"whatscanvas.canvas"];
    if (![canvas waitForExistenceWithTimeout:10.0]) return NO;
    NSPredicate *predicate = [NSPredicate predicateWithFormat:@"value == %@", @"ready"];
    XCTNSPredicateExpectation *expectation =
        [[XCTNSPredicateExpectation alloc] initWithPredicate:predicate
                                                     object:canvas];
    return [XCTWaiter waitForExpectations:@[expectation] timeout:10.0]
        == XCTWaiterResultCompleted;
}

- (BOOL)waitForLandscapeScreen
{
    NSPredicate *predicate = [NSPredicate predicateWithBlock:
        ^BOOL(id object, NSDictionary *bindings) {
            (void)object;
            (void)bindings;
            return XCUIScreen.mainScreen.screenshot.image.size.width
                > XCUIScreen.mainScreen.screenshot.image.size.height;
        }];
    XCTNSPredicateExpectation *expectation =
        [[XCTNSPredicateExpectation alloc] initWithPredicate:predicate
                                                     object:NSNull.null];
    return [XCTWaiter waitForExpectations:@[expectation] timeout:10.0]
        == XCTWaiterResultCompleted;
}

- (BOOL)waitForPortraitScreen
{
    NSPredicate *predicate = [NSPredicate predicateWithBlock:
        ^BOOL(id object, NSDictionary *bindings) {
            (void)object;
            (void)bindings;
            return XCUIScreen.mainScreen.screenshot.image.size.height
                > XCUIScreen.mainScreen.screenshot.image.size.width;
        }];
    XCTNSPredicateExpectation *expectation =
        [[XCTNSPredicateExpectation alloc] initWithPredicate:predicate
                                                     object:NSNull.null];
    return [XCTWaiter waitForExpectations:@[expectation] timeout:10.0]
        == XCTWaiterResultCompleted;
}

@end
