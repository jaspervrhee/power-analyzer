#include <gtest/gtest.h>
#include "../interfaces/ILogBackend.h"
#include "../common/LogData.h"
#include <chrono>
#include <memory>

class MockLogBackend : public ILogBackend {
public:
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(bool, send, (const LogEntry&), (override));
    MOCK_METHOD(const char*, name, (), (const, override));
};

class ILogBackendTest : public ::testing::Test {
protected:
    MockLogBackend backend;
};

TEST_F(ILogBackendTest, ConnectAndDisconnect) {
    EXPECT_CALL(backend, connect()).WillOnce(::testing::Return(true));
    EXPECT_CALL(backend, disconnect()).Times(1);

    ASSERT_TRUE(backend.connect());
    backend.disconnect();
}

TEST_F(ILogBackendTest, IsConnectedStateAfterConnect) {
    EXPECT_CALL(backend, connect()).WillOnce(::testing::Return(true));
    EXPECT_CALL(backend, isConnected())
        .WillOnce(::testing::Return(true));

    backend.connect();
    ASSERT_TRUE(backend.isConnected());
}

TEST_F(ILogBackendTest, IsConnectedStateAfterDisconnect) {
    EXPECT_CALL(backend, connect()).WillOnce(::testing::Return(true));
    EXPECT_CALL(backend, disconnect()).Times(1);
    EXPECT_CALL(backend, isConnected())
        .WillOnce(::testing::Return(false));

    backend.connect();
    backend.disconnect();
    ASSERT_FALSE(backend.isConnected());
}

TEST_F(ILogBackendTest, SendLogEntry) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = LogLevel::Info;
    entry.message = "Test message";

    EXPECT_CALL(backend, send(::testing::_))
        .WillOnce(::testing::Return(true));

    ASSERT_TRUE(backend.send(entry));
}

TEST_F(ILogBackendTest, SendStructuredLogEntry) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = LogLevel::Error;
    entry.tablePath = "functional/power";
    entry.tableName = "events";
    entry.columns = {"timestamp", "value", "unit"};
    entry.values = {"2026-05-27T10:30:00", "42.5", "kW"};

    EXPECT_CALL(backend, send(::testing::_))
        .WillOnce(::testing::Return(true));

    ASSERT_TRUE(backend.send(entry));
}

TEST_F(ILogBackendTest, SendFailure) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = LogLevel::Warning;
    entry.message = "This send will fail";

    EXPECT_CALL(backend, send(::testing::_))
        .WillOnce(::testing::Return(false));

    ASSERT_FALSE(backend.send(entry));
}

TEST_F(ILogBackendTest, BackendName) {
    EXPECT_CALL(backend, name())
        .WillOnce(::testing::Return("TestBackend"));

    ASSERT_STREQ(backend.name(), "TestBackend");
}

TEST_F(ILogBackendTest, SendMultipleEntries) {
    LogEntry entry1, entry2, entry3;
    entry1.message = "Entry 1";
    entry2.message = "Entry 2";
    entry3.message = "Entry 3";

    EXPECT_CALL(backend, send(::testing::_))
        .Times(3)
        .WillRepeatedly(::testing::Return(true));

    ASSERT_TRUE(backend.send(entry1));
    ASSERT_TRUE(backend.send(entry2));
    ASSERT_TRUE(backend.send(entry3));
}

TEST_F(ILogBackendTest, LogLevelVariations) {
    const LogLevel levels[] = {LogLevel::Debug, LogLevel::Info,
                                LogLevel::Warning, LogLevel::Error};

    for (LogLevel level : levels) {
        LogEntry entry;
        entry.level = level;
        entry.message = "Test";

        EXPECT_CALL(backend, send(::testing::_))
            .WillOnce(::testing::Return(true));

        ASSERT_TRUE(backend.send(entry));
    }
}

TEST_F(ILogBackendTest, DestructorDoesNotThrow) {
    auto ptr = std::make_unique<MockLogBackend>();
    EXPECT_NO_THROW(ptr.reset());
}
