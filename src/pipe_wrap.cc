// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "pipe_wrap.h"
#include "node.h"
#include "node_buffer.h"
#include "handle_wrap.h"
#include "node_wrap.h"
#include "req_wrap.h"
#include "stream_wrap.h"

namespace node {

using v8::Boolean;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::Persistent;
using v8::PropertyAttribute;
using v8::String;
using v8::Undefined;
using v8::Value;

static Persistent<Function> pipeConstructor;
static Cached<String> onconnection_sym;
static Cached<String> oncomplete_sym;

template <v8::Isolate** isolate> class Cache {

};

Cache<&node_isolate> c;


// TODO(bnoordhuis) share with TCPWrap?
typedef class ReqWrap<uv_connect_t> ConnectWrap;


uv_pipe_t* PipeWrap::UVHandle() {
  return &handle_;
}


Local<Object> PipeWrap::Instantiate() {
  HandleScope scope(node_isolate);
  assert(!pipeConstructor.IsEmpty());
  return scope.Close(NewInstance(pipeConstructor));
}


PipeWrap* PipeWrap::Unwrap(Local<Object> obj) {
  PipeWrap* wrap;
  NODE_UNWRAP(obj, PipeWrap, wrap);
  return wrap;
}


void PipeWrap::Initialize(Handle<Object> target) {
  StreamWrap::Initialize(target);

  HandleScope scope(node_isolate);

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(FIXED_ONE_BYTE_STRING(node_isolate, "Pipe"));

  t->InstanceTemplate()->SetInternalFieldCount(1);

  enum PropertyAttribute attributes =
      static_cast<PropertyAttribute>(v8::ReadOnly | v8::DontDelete);
  t->InstanceTemplate()->SetAccessor(FIXED_ONE_BYTE_STRING(node_isolate, "fd"),
                                     StreamWrap::GetFD,
                                     NULL,
                                     Handle<Value>(),
                                     v8::DEFAULT,
                                     attributes);

  NODE_SET_PROTOTYPE_METHOD(t, "close", HandleWrap::Close);
  NODE_SET_PROTOTYPE_METHOD(t, "unref", HandleWrap::Unref);
  NODE_SET_PROTOTYPE_METHOD(t, "ref", HandleWrap::Ref);

  NODE_SET_PROTOTYPE_METHOD(t, "readStart", StreamWrap::ReadStart);
  NODE_SET_PROTOTYPE_METHOD(t, "readStop", StreamWrap::ReadStop);
  NODE_SET_PROTOTYPE_METHOD(t, "shutdown", StreamWrap::Shutdown);

  NODE_SET_PROTOTYPE_METHOD(t, "writeBuffer", StreamWrap::WriteBuffer);
  NODE_SET_PROTOTYPE_METHOD(t,
                            "writeAsciiString",
                            StreamWrap::WriteAsciiString);
  NODE_SET_PROTOTYPE_METHOD(t, "writeUtf8String", StreamWrap::WriteUtf8String);
  NODE_SET_PROTOTYPE_METHOD(t, "writeUcs2String", StreamWrap::WriteUcs2String);

  NODE_SET_PROTOTYPE_METHOD(t, "bind", Bind);
  NODE_SET_PROTOTYPE_METHOD(t, "listen", Listen);
  NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
  NODE_SET_PROTOTYPE_METHOD(t, "open", Open);

#ifdef _WIN32
  NODE_SET_PROTOTYPE_METHOD(t, "setPendingInstances", SetPendingInstances);
#endif

  pipeConstructorTmpl.Reset(node_isolate, t);
  pipeConstructor.Reset(node_isolate, t->GetFunction());
  target->Set(FIXED_ONE_BYTE_STRING(node_isolate, "Pipe"), t->GetFunction());
}


void PipeWrap::New(const FunctionCallbackInfo<Value>& args) {
  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  assert(args.IsConstructCall());

  HandleScope scope(node_isolate);
  PipeWrap* wrap = new PipeWrap(args.This(), args[0]->IsTrue());
  assert(wrap);
}


PipeWrap::PipeWrap(Handle<Object> object, bool ipc)
    : StreamWrap(object, reinterpret_cast<uv_stream_t*>(&handle_)) {
  int r = uv_pipe_init(uv_default_loop(), &handle_, ipc);
  assert(r == 0);  // How do we proxy this error up to javascript?
                   // Suggestion: uv_pipe_init() returns void.
  UpdateWriteQueueSize();
}


void PipeWrap::Bind(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);

  PipeWrap* wrap;
  NODE_UNWRAP(args.This(), PipeWrap, wrap);

  String::AsciiValue name(args[0]);
  int err = uv_pipe_bind(&wrap->handle_, *name);
  args.GetReturnValue().Set(err);
}


#ifdef _WIN32
void PipeWrap::SetPendingInstances(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);

  PipeWrap* wrap;
  NODE_UNWRAP(args.This(), PipeWrap, wrap);

  int instances = args[0]->Int32Value();

  uv_pipe_pending_instances(&wrap->handle_, instances);
}
#endif


void PipeWrap::Listen(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);

  PipeWrap* wrap;
  NODE_UNWRAP(args.This(), PipeWrap, wrap);

  int backlog = args[0]->Int32Value();
  int err = uv_listen(reinterpret_cast<uv_stream_t*>(&wrap->handle_),
                      backlog,
                      OnConnection);
  args.GetReturnValue().Set(err);
}


// TODO(bnoordhuis) maybe share with TCPWrap?
void PipeWrap::OnConnection(uv_stream_t* handle, int status) {
  HandleScope scope(node_isolate);

  PipeWrap* pipe_wrap = static_cast<PipeWrap*>(handle->data);
  assert(&pipe_wrap->handle_ == reinterpret_cast<uv_pipe_t*>(handle));

  // We should not be getting this callback if someone as already called
  // uv_close() on the handle.
  assert(pipe_wrap->persistent().IsEmpty() == false);

  Local<Value> argv[] = {
    Integer::New(status, node_isolate),
    Undefined()
  };

  if (status != 0) {
    MakeCallback(pipe_wrap->object(), "onconnection", ARRAY_SIZE(argv), argv);
    return;
  }

  // Instanciate the client javascript object and handle.
  Local<Object> client_obj = NewInstance(pipeConstructor);

  // Unwrap the client javascript object.
  PipeWrap* wrap;
  NODE_UNWRAP(client_obj, PipeWrap, wrap);
  uv_stream_t* client_handle = reinterpret_cast<uv_stream_t*>(&wrap->handle_);
  if (uv_accept(handle, client_handle))
    return;

  // Successful accept. Call the onconnection callback in JavaScript land.
  argv[1] = client_obj;
  if (onconnection_sym.IsEmpty()) {
    onconnection_sym = FIXED_ONE_BYTE_STRING(node_isolate, "onconnection");
  }
  MakeCallback(pipe_wrap->object(), onconnection_sym, ARRAY_SIZE(argv), argv);
}

// TODO(bnoordhuis) Maybe share this with TCPWrap?
void PipeWrap::AfterConnect(uv_connect_t* req, int status) {
  ConnectWrap* req_wrap = static_cast<ConnectWrap*>(req->data);
  PipeWrap* wrap = static_cast<PipeWrap*>(req->handle->data);

  HandleScope scope(node_isolate);

  // The wrap and request objects should still be there.
  assert(req_wrap->persistent().IsEmpty() == false);
  assert(wrap->persistent().IsEmpty() == false);

  bool readable, writable;

  if (status) {
    readable = writable = 0;
  } else {
    readable = uv_is_readable(req->handle) != 0;
    writable = uv_is_writable(req->handle) != 0;
  }

  Local<Object> req_wrap_obj = req_wrap->object();
  Local<Value> argv[5] = {
    Integer::New(status, node_isolate),
    wrap->object(),
    req_wrap_obj,
    Boolean::New(readable),
    Boolean::New(writable)
  };

  if (oncomplete_sym.IsEmpty()) {
    oncomplete_sym = FIXED_ONE_BYTE_STRING(node_isolate, "oncomplete");
  }
  MakeCallback(req_wrap_obj, oncomplete_sym, ARRAY_SIZE(argv), argv);

  delete req_wrap;
}


void PipeWrap::Open(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);

  PipeWrap* wrap;
  NODE_UNWRAP(args.This(), PipeWrap, wrap);

  int fd = args[0]->Int32Value();

  uv_pipe_open(&wrap->handle_, fd);
}


void PipeWrap::Connect(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);

  PipeWrap* wrap;
  NODE_UNWRAP(args.This(), PipeWrap, wrap);

  assert(args[0]->IsObject());
  assert(args[1]->IsString());

  Local<Object> req_wrap_obj = args[0].As<Object>();
  String::AsciiValue name(args[1]);

  ConnectWrap* req_wrap = new ConnectWrap(req_wrap_obj);
  uv_pipe_connect(&req_wrap->req_,
                  &wrap->handle_,
                  *name,
                  AfterConnect);
  req_wrap->Dispatched();

  args.GetReturnValue().Set(0);  // uv_pipe_connect() doesn't return errors.
}


}  // namespace node

NODE_MODULE(node_pipe_wrap, node::PipeWrap::Initialize)
