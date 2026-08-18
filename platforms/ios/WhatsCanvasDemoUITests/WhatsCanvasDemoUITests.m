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
    [app launch];
    XCTAssertTrue([app waitForState:XCUIApplicationStateRunningForeground
                            timeout:10.0]);

    XCTAttachment *portrait = [XCTAttachment attachmentWithScreenshot:
        XCUIScreen.mainScreen.screenshot];
    portrait.name = @"Portrait";
    portrait.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:portrait];

    XCUIDevice.sharedDevice.orientation = UIDeviceOrientationLandscapeLeft;
    XCTAssertTrue([self waitForLandscapeScreen]);
    XCTAttachment *landscape = [XCTAttachment attachmentWithScreenshot:
        XCUIScreen.mainScreen.screenshot];
    landscape.name = @"Landscape";
    landscape.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:landscape];

    [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];
    XCTAssertTrue([app waitForState:XCUIApplicationStateRunningBackground
                            timeout:5.0]);
    [app activate];
    XCTAssertTrue([app waitForState:XCUIApplicationStateRunningForeground
                            timeout:10.0]);

    [app terminate];
    XCTAssertTrue([app waitForState:XCUIApplicationStateNotRunning
                            timeout:5.0]);
    [app launch];
    XCTAssertTrue([app waitForState:XCUIApplicationStateRunningForeground
                            timeout:10.0]);
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

@end
