typedef struct gbinder_remote_request GBinderRemoteRequest;

void
gbinder_remote_request_unref(
    GBinderRemoteRequest* req)
{
    __coverity_free__(req);
}
