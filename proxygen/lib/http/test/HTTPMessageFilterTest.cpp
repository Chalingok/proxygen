/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/HTTPMessageFilters.h>
#include <proxygen/lib/http/test/MockHTTPMessageFilter.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>

using namespace proxygen;

class TestFilter : public HTTPMessageFilter {
  std::unique_ptr<HTTPMessageFilter> clone () noexcept override {
    return nullptr;
  }
};

TEST(HTTPMessageFilter, TestFilterPauseResumePropagatedToFilter) {
  //              prev               prev
  // testFilter2 -----> testFilter1 -----> mockFilter

  TestFilter testFilter1;
  TestFilter testFilter2;
  MockHTTPMessageFilter mockFilter;

  testFilter2.setPrevFilter(&testFilter1);
  testFilter1.setPrevFilter(&mockFilter);

  EXPECT_CALL(mockFilter, pause());
  testFilter2.pause();

  EXPECT_CALL(mockFilter, resume(10));
  testFilter2.resume(10);
}

TEST(HTTPMessageFilter, TestFilterPauseResumePropagatedToTxn) {
  //              prev               prev
  // testFilter2 -----> testFilter1 -----> mockFilter

  TestFilter testFilter1;
  TestFilter testFilter2;

  HTTP2PriorityQueue q;
  MockHTTPTransaction mockTxn(TransportDirection::UPSTREAM, 1, 0, q);

  testFilter2.setPrevFilter(&testFilter1);
  testFilter1.setPrevTxn(&mockTxn);

  EXPECT_CALL(mockTxn, pauseIngress());
  testFilter2.pause();

  EXPECT_CALL(mockTxn, resumeIngress());
  testFilter2.resume(10);
}

TEST(HTTPMessageFilter, TestFilterOnBodyDataTracking) {
  //             next
  // testFilter -----> mockFilter

  TestFilter testFilter;
  MockHTTPMessageFilter mockFilter;
  mockFilter.setTrackDataPassedThrough(true);

  testFilter.setNextTransactionHandler(&mockFilter);

  EXPECT_CALL(mockFilter, onBody(testing::_)).Times(1);

  std::string bodyContent = "Hello";
  auto body = folly::IOBuf::copyBuffer(bodyContent);
  testFilter.onBody(std::move(body));
  auto dataPassedToNext = mockFilter.bodyDataSinceLastCheck();
  dataPassedToNext->coalesce();
  auto len = dataPassedToNext->computeChainDataLength();
  EXPECT_EQ(bodyContent.size(), len);
  const char* p = reinterpret_cast<const char*>(dataPassedToNext->data());
  EXPECT_EQ(bodyContent, std::string(p, len));

  EXPECT_CALL(mockFilter, onBody(testing::_)).Times(1);

  bodyContent = "World";
  body = folly::IOBuf::copyBuffer(bodyContent);
  testFilter.onBody(std::move(body));
  dataPassedToNext = mockFilter.bodyDataSinceLastCheck();
  dataPassedToNext->coalesce();
  len = dataPassedToNext->computeChainDataLength();
  EXPECT_EQ(bodyContent.size(), len);
  p = reinterpret_cast<const char*>(dataPassedToNext->data());
  EXPECT_EQ(bodyContent, std::string(p, len));
}

TEST(HTTPMessageFilter, TestFilterPauseResumeAfterTxnDetached) {
  //              prev               prev               prev
  // testFilter2 -----> mockFilter -----> testFilter1 -----> mockTxn

  TestFilter testFilter1;
  TestFilter testFilter2;
  MockHTTPMessageFilter mockFilter;

  HTTP2PriorityQueue q;
  MockHTTPTransaction mockTxn(TransportDirection::UPSTREAM, 1, 0, q);

  testFilter2.setPrevFilter(&mockFilter);
  mockFilter.setPrevFilter(&testFilter1);
  testFilter1.setPrevTxn(&mockTxn);

  testFilter1.setNextTransactionHandler(&mockFilter);
  mockFilter.setNextTransactionHandler(&testFilter2);

  testFilter1.detachTransaction();

  EXPECT_CALL(mockTxn, pauseIngress()).Times(0);
  EXPECT_CALL(mockFilter, pause());
  testFilter2.pause();

  EXPECT_CALL(mockTxn, resumeIngress()).Times(0);
  EXPECT_CALL(mockFilter, resume(10));
  testFilter2.resume(10);
}
