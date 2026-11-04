import unittest
import helpers
from audit_district_routes import audit,distances


class DistrictRouteTests(unittest.TestCase):
    def test_reports_disconnected_district_and_respects_direction(self):
        nodes={1:(0,0),2:(20,0),3:(100,0)}
        graph={1:[(2,20)],2:[],3:[]}
        districts=[{'id':name,'bounds':[[x-1,-1],[x+1,-1],[x+1,1],[x-1,1]]} for name,x in [('first',0),('second',20),('third',100)]]
        result=audit(nodes,graph,districts)
        self.assertFalse(result['connected'])
        self.assertEqual(distances(graph,1),{1:0,2:20})
        self.assertEqual(distances(graph,2),{2:0})
