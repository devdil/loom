/*
 * Copyright (c) 2020, 2021, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <string.h>
#include "jvmti.h"

extern "C" {

static jvmtiEnv *jvmti = NULL;
static volatile jboolean is_completed_test_in_event = JNI_FALSE;

static void
fatal(JNIEnv* jni, const char* msg) {
  jni->FatalError(msg);
  fflush(stdout);
}

static void
check(JNIEnv* jni, const char* msg, jvmtiError err) {
  if (err != JVMTI_ERROR_NONE) {
    printf("%s failed with error code %d\n", msg, err);
    fatal(jni, msg);
  }
}

static void
check_jvmti_error_invalid_thread(JNIEnv* jni, const char* msg, jvmtiError err) {
  if (err != JVMTI_ERROR_INVALID_THREAD) {
    printf("%s failed: expected JVMTI_ERROR_INVALID_THREAD instead of: %d\n", msg, err);
    fatal(jni, msg);
  }
}

JNIEXPORT jboolean JNICALL
Java_VThreadUnsupportedTest_isCompletedTestInEvent(JNIEnv *env, jobject obj) {
  return is_completed_test_in_event;
}

/*
 * Execute JVMTI functions which currently don't support vthreads and check that
 * they return error code JVMTI_ERROR_INVALID_THREAD correctly.
 */
static void
test_unsupported_jvmti_functions(jvmtiEnv *jvmti, JNIEnv *jni, jthread vthread) {
  jvmtiError err;
  jboolean is_vthread;
  jvmtiCapabilities caps;
  void* local_storage_data = NULL;
  jlong nanos;

  printf("test_unsupported_jvmti_functions: started\n");
  fflush(0);

  is_vthread = jni->IsVirtualThread(vthread);
  if (is_vthread != JNI_TRUE) {
    fatal(jni, "IsVirtualThread failed to return JNI_TRUE");
  }

  err = jvmti->GetCapabilities(&caps);
  check(jni, "GetCapabilities", err);

  if (caps.can_support_virtual_threads != JNI_TRUE) {
    fatal(jni, "Virtual threads are not supported");
  }

  printf("Testing JVMTI functions which should not accept a virtual thread argument\n");
  fflush(0);

  err = jvmti->StopThread(vthread, vthread);
  check_jvmti_error_invalid_thread(jni, "StopThread", err);
  
  err = jvmti->InterruptThread(vthread);
  check_jvmti_error_invalid_thread(jni, "InterruptThread", err);
 
  err = jvmti->PopFrame(vthread);
  check_jvmti_error_invalid_thread(jni, "PopFrame", err);
  
  err = jvmti->ForceEarlyReturnVoid(vthread);
  check_jvmti_error_invalid_thread(jni, "ForceEarlyReturnVoid", err);

  err = jvmti->GetThreadCpuTime(vthread, &nanos);
  check_jvmti_error_invalid_thread(jni, "GetThreadCpuTime", err);

  printf("test_unsupported_jvmti_functions: finished\n");
  fflush(0);
}

JNIEXPORT jboolean JNICALL
Java_VThreadUnsupportedTest_testJvmtiFunctionsInJNICall(JNIEnv *jni, jobject obj) {
  jvmtiError err = JVMTI_ERROR_NONE;
  jint threads_count = 0;
  jthread *threads = NULL;
  jthread cthread = NULL;

  printf("testJvmtiFunctionsInJNICall: started\n");
  fflush(0);

  err = jvmti->GetCurrentThread(&cthread);
  check(jni, "GetCurrentThread", err);
  printf("\n#### GetCurrentThread returned thread: %p\n", (void*)cthread);
  fflush(0);

  err = jvmti->GetAllThreads(&threads_count, &threads);
  check(jni, "GetAllThreads", err);

  for (int thread_idx = 0; thread_idx < (int) threads_count; thread_idx++) {
    jthread thread = threads[thread_idx];
    jthread vthread;

    jvmtiThreadInfo thr_info;
    jvmtiError err = jvmti->GetThreadInfo(thread, &thr_info);
    check(jni, "GetThreadInfo", err);

    const char* thr_name = (thr_info.name == NULL) ? "<Unnamed thread>" : thr_info.name;
    if (jni->IsSameObject(cthread, thread) == JNI_TRUE) {
      continue;
    }
    err = jvmti->SuspendThread(thread);
    if (err == JVMTI_ERROR_THREAD_NOT_ALIVE) {
      continue;
    }
    check(jni, "SuspendThread", err);

    err = jvmti->GetVirtualThread(thread, &vthread);
    if (err == JVMTI_ERROR_THREAD_NOT_SUSPENDED) {
      // Some system threads might not fully suspended. so just skip them
      err = jvmti->ResumeThread(thread);
      check(jni, "ResumeThread", err);
      continue;
    }
    check(jni, "GetVirtualThread", err);
    if (vthread != NULL) {
      printf("\n#### Found carrier thread: %s\n", thr_name);
      fflush(stdout);
      test_unsupported_jvmti_functions(jvmti, jni, vthread);
    }
    err = jvmti->ResumeThread(thread);
    check(jni, "ResumeThread", err);
  }
  printf("testJvmtiFunctionsInJNICall: finished\n");
  fflush(0);
  return JNI_TRUE;
}

static void JNICALL
VirtualThreadMounted(jvmtiEnv *jvmti, JNIEnv *jni, jthread vthread) {
  printf("Got VirtualThreadMounted event\n");
  fflush(stdout);
  test_unsupported_jvmti_functions(jvmti, jni, vthread);
  is_completed_test_in_event = JNI_TRUE;
}

extern JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
  jvmtiEventCallbacks callbacks;
  jvmtiCapabilities caps;
  jvmtiError err;

  printf("Agent_OnLoad started\n");
  if (jvm->GetEnv((void **)(&jvmti), JVMTI_VERSION) != JNI_OK) {
    return JNI_ERR;
  }

  memset(&callbacks, 0, sizeof (callbacks));
  callbacks.VirtualThreadMounted = &VirtualThreadMounted;

  memset(&caps, 0, sizeof (caps));
  caps.can_suspend = 1;
  caps.can_pop_frame = 1;
  caps.can_force_early_return = 1;
  caps.can_signal_thread = 1;
  caps.can_support_virtual_threads = 1;
  caps.can_access_local_variables = 1;
  caps.can_get_thread_cpu_time = 1;

  err = jvmti->AddCapabilities(&caps);
  if (err != JVMTI_ERROR_NONE) {
    printf("error in JVMTI AddCapabilities: %d\n", err);
  }

  err = jvmti->SetEventCallbacks(&callbacks, sizeof (jvmtiEventCallbacks));
  if (err != JVMTI_ERROR_NONE) {
    printf("error in JVMTI SetEventCallbacks: %d\n", err);
  }

  err = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VIRTUAL_THREAD_MOUNTED, NULL);
  if (err != JVMTI_ERROR_NONE) {
    printf("error in JVMTI SetEventNotificationMode: %d\n", err);
  }

  printf("Agent_OnLoad finished\n");
  return 0;
}

} // extern "C"
