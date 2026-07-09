/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Force libcamera to load its IPA (Image Processing Algorithm) module
 * in-process instead of in an isolated worker process.
 *
 * Why this is needed
 * ------------------
 * The prebuilt `ros-jazzy-libcamera` package signs its IPA modules (e.g.
 * `ipa_rpi_vc4.so`) at build time, but ROS packaging edits the shared objects
 * (rpath / patchelf) *after* signing. That invalidates the signature, so
 * `libcamera::IPAManager::isSignatureValid()` returns false and libcamera falls
 * back to running the IPA in an isolated child process. In this container that
 * isolated worker crashes while deserializing controls at start
 * (`ControlSerializer::deserialize<ControlList>` -> "Failed to call start:
 * -110"), so no frames are ever produced.
 *
 * The IPA module itself is the genuine, unmodified libcamera Raspberry Pi IPA,
 * so loading it in-process is safe. When `LD_PRELOAD`ed into the camera process,
 * this interposes `IPAManager::isSignatureValid()` to return true, which makes
 * libcamera load the IPA in the same process (no IPC, no isolated worker, no
 * crash).
 *
 * Scope: this only affects processes that explicitly `LD_PRELOAD` this library
 * (the camera node launched by this package). It is a container/packaging
 * workaround; a genuine Raspberry Pi OS libcamera build loads the IPA
 * in-process on its own and does not need it.
 *
 * The exported symbol is the mangled form of:
 *   bool libcamera::IPAManager::isSignatureValid(libcamera::IPAModule *) const
 */
extern "C" __attribute__((visibility("default")))
bool _ZNK9libcamera10IPAManager16isSignatureValidEPNS_9IPAModuleE(void *self, void *ipa_module)
{
  (void)self;
  (void)ipa_module;
  return true;
}
