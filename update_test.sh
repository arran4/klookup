cat << 'INNER_EOF' > patch_test.diff
--- tests/test_mainwindow.cpp
+++ tests/test_mainwindow.cpp
@@ -348,24 +348,16 @@
     {
         // Proves that if KWallet::isOpen() reports false, we don't even attempt to
         // open the wallet, preventing a blocking UI prompt.
-        bool isOpenCalled = false;
-        std::function<bool(const QString&)> mockIsOpen = [&isOpenCalled](const QString&) {
-            isOpenCalled = true;
-            return false; // act as if closed
-        };
-
-        MainWindow mainWindow(nullptr, mockIsOpen);
+        TestableMainWindow mainWindow;

-        // We can explicitly test the seam by appending a mock provider and manually
-        // calling loadPlugins() again. Since loadPlugins skips duplicates, we use a mock with a unique ID.
         AuthMockProvider authMock;
-        mainWindow.m_providers.append(&authMock);
+        mainWindow.loadWalletCredentials(&authMock);

-        // We simulate what loadPlugins does because we can't easily force it to discover the mock plugin
-        // from a .so file during tests without relying on the external test environment's build output.
-        // The core logic we want to test is the wallet condition check for auth providers.
-        if (authMock.supportsAuthentication()) {
-            if (mainWindow.m_walletIsOpenFn) {
-                mainWindow.m_walletIsOpenFn(QStringLiteral("kdewallet"));
-            }
-        }
-
-        QVERIFY(isOpenCalled);
+        QVERIFY(mainWindow.m_isOpenCalled);
+        QVERIFY(!mainWindow.m_openWalletCalled);
+        QVERIFY(!mainWindow.m_clipboardNotificationEmitted);
     }
INNER_EOF
patch tests/test_mainwindow.cpp < patch_test.diff
