#!/usr/bin/env python

'''Generate declaration/definitions for the update() call.

C++ is a pain.
'''

tmpl = '''
  template<%(classes)s, void (*Fn)(%(decls)s, UpdateMap&, UpdateMap&)>
  class UpdateFunction%(arity)s: public UpdateFunction {
  private:
    %(ivar_decls)s
  public:
    virtual void read_values(SendRecvHelper& rpc, int src) {
      // rpc.recv_pod(src, &a_);
      %(recvs)s
    }

    virtual void operator()(UpdateMap& up, UpdateMap& global) {
      %(call)s
    }
  };

  template<%(classes)s, void (*Fn)(%(decls)s, UpdateMap&, UpdateMap&)>
  void update(%(decls)s) {
    static UpdateFunctionRegistry::Helper<UpdateFunction%(arity)s<%(classnames)s, Fn> > register_me;
    {
      SendRecvHelper send(kWorkerData, *network_);
      %(sends)s
      worker_send_update(send, register_me.id());
    }

    {
      SendRecvHelper recv(kSyncerData, *network_);
      worker_recv_state(recv);
    }
  }
'''

for arity in range(1, 9):
  classname = lambda i: chr(ord('A') + i)
  ivarname = lambda i: chr(ord('a') + i) + '_'
  varname = lambda i: chr(ord('a') + i)

  classes = ','.join(['class %s' % classname(i) for i in range(arity)])
  classnames = ','.join(['%s' % classname(i) for i in range(arity)])

  ivar_decls = ';\n    '.join(['%s %s' % (classname(i), ivarname(i)) for i in range(arity)]) + ';'
  ivars = ','.join([ivarname(i) for i in range(arity)])

  decls = ','.join(['const %s& %s' % (classname(i), varname(i)) for i in range(arity)])
  sends = ';\n      '.join(['send.send_all(%s)' % varname(i) for i in range(arity)]) + ';'
  recvs = ';\n      '.join(['rpc.recv_pod(src, &%s)' % ivarname(i) for i in range(arity)]) + ';'
  call = 'Fn(%s, up, global);' % ivars
  
  print tmpl % locals()
