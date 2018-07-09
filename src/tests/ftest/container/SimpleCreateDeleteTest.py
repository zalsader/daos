#!/usr/bin/python
'''
  (C) Copyright 2017 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''

import os
import time
import traceback
import sys
import json

from avocado       import Test
from avocado       import main
from avocado.utils import process

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import ServerUtils
import CheckForPool
import WriteHostFile
import daos_api
from daos_api import DaosContext
from daos_api import DaosPool
from daos_api import DaosContainer
from daos_api import RankList


class SimpleCreateDeleteTest(Test):
    """
    Tests DAOS container basics including create, destroy, open, query
    and close.

    :avocado: tags=container,containercreate,containerdestroy,basecont
    """
    def setUp(self):

       # get paths from the build_vars generated by build
       with open('../../../.build_vars.json') as f:
           build_paths = json.load(f)
       self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
       self.tmp = build_paths['PREFIX'] + '/tmp'

       self.server_group = self.params.get("server_group",'/server/',
                                           'daos_server')

       # setup the DAOS python API
       self.Context = DaosContext(build_paths['PREFIX'] + '/lib/')

    def tearDown(self):
        pass

    def test_container_basics(self):
        """
        Test basic container create/destroy/open/close/query.  Nothing fancy
        just making sure they work at a rudimentary level

        :avocado: tags=container,containercreate,containerdestroy,basecont
        """

        hostfile = None

        try:
            hostlist = self.params.get("test_machines",'/run/hosts/*')
            hostfile = WriteHostFile.WriteHostFile(hostlist, self.tmp)

            ServerUtils.runServer(hostfile, self.server_group, self.basepath)

            # give it time to start
            time.sleep(2)

            # parameters used in pool create
            createmode = self.params.get("mode",'/run/conttests/createmode/')
            createuid  = self.params.get("uid",'/run/conttests/createuid/')
            creategid  = self.params.get("gid",'/run/conttests/creategid/')
            createsetid = self.params.get("setname",'/run/conttests/createset/')
            createsize  = self.params.get("size",'/run/conttests/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            POOL = DaosPool(self.Context)
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # need a connection to create container
            POOL.connect(1 << 1)

            # create a container
            CONTAINER = DaosContainer(self.Context)
            CONTAINER.create(POOL.handle)

            # now open it
            CONTAINER.open()

            # do a query and compare the UUID returned from create with
            # that returned by query
            CONTAINER.query()

            if CONTAINER.get_uuid_str() != daos_api.c_uuid_to_str(
                    CONTAINER.info.ci_uuid):
                self.fail("Container UUID did not match the one in info'n")

            CONTAINER.close()

            # wait a few seconds and then destroy
            time.sleep(5)
            CONTAINER.destroy()

            # cleanup the pool
            POOL.disconnect()
            POOL.destroy(1)

        except ValueError as e:
            print e
            print traceback.format_exc()
            self.fail("Test was expected to pass but it failed.\n")
        except Exception as e:
            self.fail("Daos code segfaulted most likely, error: %s" % e)
        finally:
            ServerUtils.stopServer()
            if hostfile is not None:
                os.remove(hostfile)

if __name__ == "__main__":
    main()
