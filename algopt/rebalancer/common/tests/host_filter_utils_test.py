#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


# pyre-strict
# pyre-ignore-all-errors[56]

import asyncio
from unittest.mock import MagicMock, patch

from algopt.rebalancer.common.host_filter_utils import (
    gen_decomed_assets,
    get_serf_filters_for_movable_servers,
    query_ats_for_lemon_status,
    query_els_for_tupperware_non_prod_hosts,
    query_serf_for_asset_ids,
    query_serf_for_datacenter_names,
)
from facebook.core_systems.datacenter.thrift_types import Datacenter
from facebook.core_systems.device.thrift_types import Device
from facebook.core_systems.queries.thrift_types import BinaryExpression, Query
from libfb.py.asyncio.mock import AsyncMock
from libfb.py.asyncio.unittest import async_test
from libfb.py.testutil import BaseFacebookTestCase
from smc.ServiceManager.thrift_types import Service
from tupperware.experiment_ledger.thrift_types import ExperimentAsset


BASE_PATH = "algopt.rebalancer.common.host_filter_utils."


# pyre-fixme[3]: Return type must be annotated.
# pyre-fixme[2]: Parameter must be annotated.
def async_return(result):
    f = asyncio.Future()
    f.set_result(result)
    return f


class AsyncMagicMock(MagicMock):
    # pyre-fixme[3]: Return type must be annotated.
    # pyre-fixme[2]: Parameter must be annotated.
    async def __aenter__(self, *args, **kwargs):
        return self

    # pyre-fixme[2]: Parameter must be annotated.
    async def __aexit__(self, *args, **kwargs) -> None:
        pass


class TestHostFilterUtils(BaseFacebookTestCase):
    @patch(BASE_PATH + "logging.exception")
    @patch(BASE_PATH + "get_asr_client", new_callable=AsyncMagicMock)
    @patch(BASE_PATH + "get_all_child_services", new_callable=AsyncMagicMock)
    @async_test
    async def test_query_els_for_tupperware_non_prod_hosts_exception(
        self,
        mock_get_all_child_services: MagicMock,
        mock_get_py3_client: MagicMock,
        mock_logging_exception: MagicMock,
    ) -> None:
        # arrange
        client = AsyncMock()
        client.search_assets.side_effect = Exception("Foo")

        mock_get_all_child_services.return_value = async_return(
            [
                Service(hostname="twshared01"),
                Service(hostname="twshared02"),
            ]
        )
        mock_get_py3_client.return_value = client

        # act
        ret = await query_els_for_tupperware_non_prod_hosts()

        # assert
        self.assertEqual({"twshared01", "twshared02"}, ret)

        mock_logging_exception.assert_called_once()
        mock_logging_exception.assert_called_with(
            "Failed to get all twshared experimental hosts: Foo"
        )

    @patch(BASE_PATH + "logging.exception")
    @patch(BASE_PATH + "get_asr_client", new_callable=AsyncMagicMock)
    @patch(BASE_PATH + "get_all_child_services", new_callable=AsyncMagicMock)
    @async_test
    async def test_query_ats_for_lemon_status_exception(
        self,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_get_all_child_services,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_get_py3_client,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_logging_exception,
    ) -> None:
        # arrange
        client = AsyncMock()
        client.getAssetIds.side_effect = Exception("Foo")

        mock_get_all_child_services.return_value = async_return(
            [
                Service(hostname="twshared01"),
                Service(hostname="twshared02"),
            ]
        )
        mock_get_py3_client.return_value = client

        # act
        ret = await query_ats_for_lemon_status()

        # assert
        self.assertEqual(set(), ret)

        mock_logging_exception.assert_called_once()
        mock_logging_exception.assert_called_with("Failed to get lemon assets: Foo")

    @patch(BASE_PATH + "get_asr_client", new_callable=AsyncMagicMock)
    @patch(BASE_PATH + "get_all_child_services", new_callable=AsyncMagicMock)
    @async_test
    async def test_query_els_for_tupperware_non_prod_hosts_happy_path(
        self,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_get_all_child_services,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_get_py3_client,
    ) -> None:
        # arrange
        client = AsyncMock()
        client.search_assets.side_effect = [[ExperimentAsset(hostname="twshared02")]]
        mock_get_py3_client.return_value = client

        mock_get_all_child_services.return_value = async_return(
            [
                Service(hostname="twshared01"),
                Service(hostname="twshared02"),
            ]
        )

        # act
        ret = await query_els_for_tupperware_non_prod_hosts()

        # assert
        self.assertEqual({"twshared01", "twshared02"}, ret)

    def test_get_serf_filters_for_movable_servers(self) -> None:
        filters: list[BinaryExpression] = get_serf_filters_for_movable_servers()

        self.assertEqual(len(filters), 7)

    @patch(
        BASE_PATH + "get_location_filters_for_upcoming_migrations_v2",
    )
    @patch(BASE_PATH + "query_serf_for_asset_ids")
    @patch(BASE_PATH + "query_serf_for_datacenter_names")
    @async_test
    async def test_gen_decomed_asset_ids(
        self,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_query_serf_for_datacenter_names,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_query_serf_for_asset_ids,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_get_location_filters_for_upcoming_migrations,
    ) -> None:
        upcoming_migration_filters = [
            {
                "datacenter": "prn2",
                "suite": "1A",
                "cluster": "03",
                "row": ["1B", "1C"],
                "rack_position": ["10"],
            },
            {
                "datacenter": "prn2",
                "suite": "1A",
                "cluster": "02",
                "row": ["10A"],
            },
        ]

        mock_get_location_filters_for_upcoming_migrations.return_value = AsyncMock(
            return_value=upcoming_migration_filters
        )
        mock_query_serf_for_asset_ids.return_value = {Device(id=3, asset_id=3)}
        mock_query_serf_for_datacenter_names.return_value = ["prn2"]

        self.assertEqual(
            await gen_decomed_assets("prn", [100]), {Device(id=3, asset_id=3)}
        )

    @patch(
        BASE_PATH + "get_location_filters_for_upcoming_migrations_v2",
    )
    @patch(BASE_PATH + "query_serf_for_asset_ids")
    @patch(BASE_PATH + "query_serf_for_datacenter_names")
    @async_test
    async def test_gen_decomed_asset_ids_no_datacenters(
        self,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_query_serf_for_datacenter_names,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_query_serf_for_asset_ids,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_get_location_filters_for_upcoming_migrations,
    ) -> None:
        upcoming_migration_filters = [
            {
                "datacenter": "prn2",
                "suite": "1A",
                "cluster": "03",
                "row": ["1B", "1C"],
                "rack_position": ["10"],
            },
            {
                "datacenter": "prn2",
                "suite": "1A",
                "cluster": "02",
                "row": ["10A"],
            },
        ]

        mock_get_location_filters_for_upcoming_migrations.return_value = AsyncMock(
            return_value=upcoming_migration_filters
        )
        mock_query_serf_for_asset_ids.return_value = {Device(id=3, asset_id=3)}
        mock_query_serf_for_datacenter_names.return_value = []

        self.assertEqual(await gen_decomed_assets("prn", [100]), set())

    @patch(BASE_PATH + "logging.exception")
    @patch(BASE_PATH + "Serf3ServiceClient", new_callable=AsyncMagicMock)
    @async_test
    async def test_query_serf_for_asset_ids_exception(
        self,
        mock_serf_client: MagicMock,
        mock_logging_exception: MagicMock,
    ) -> None:
        client = AsyncMock()
        client.getDevices.side_effect = Exception("Test")
        mock_serf_client.return_value = client

        ret = await query_serf_for_asset_ids(Query(binExprs=[]))

        mock_logging_exception.assert_called_once()
        mock_logging_exception.assert_called_with("Failed to get serf devices: Test")
        self.assertEqual(ret, set())

    @patch(BASE_PATH + "Serf3ServiceClient", new_callable=AsyncMagicMock)
    @async_test
    async def test_query_serf_for_asset_ids(
        self,
        mock_serf_client: MagicMock,
    ) -> None:
        client = AsyncMock()
        client.getDevices.return_value = [
            Device(id=3, asset_id=3),
            Device(id=10, asset_id=10),
        ]
        mock_serf_client.return_value = client

        ret = await query_serf_for_asset_ids(Query(binExprs=[]))

        self.assertEqual(ret, {Device(id=3, asset_id=3), Device(id=10, asset_id=10)})

    @patch(BASE_PATH + "logging.exception")
    @patch(BASE_PATH + "Serf3ServiceClient", new_callable=AsyncMagicMock)
    @async_test
    async def test_query_serf_for_datacenter_names_exception(
        self,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_serf_client,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_logging_exception,
    ) -> None:
        client = AsyncMock()
        client.getDevices.side_effect = Exception("Test")
        mock_serf_client.return_value = client

        ret = await query_serf_for_datacenter_names(Query(binExprs=[]))

        mock_logging_exception.assert_called_once()
        self.assertEqual(ret, [])

    @patch(BASE_PATH + "Serf3ServiceClient", new_callable=AsyncMagicMock)
    @async_test
    async def test_query_serf_for_datacenter_names(
        self,
        # pyre-fixme[2]: Parameter must be annotated.
        mock_serf_client,
    ) -> None:
        client = AsyncMock()
        client.getDatacenters.return_value = [
            Datacenter(id=1, name="prn0"),
            Datacenter(id=2, name="prn2"),
        ]
        mock_serf_client.return_value = client

        ret = await query_serf_for_datacenter_names(Query(binExprs=[]))

        self.assertEqual(ret, ["prn0", "prn2"])
