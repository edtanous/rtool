#include <boost/json.h>

void PrettyPrint(std::ostream& os, boost::json::value const& jv,
                 std::string* indent = nullptr) {
  std::string local_indent;
  if (indent == nullptr) {
    indent = &local_indent;
  }
  switch (jv.kind()) {
    case boost::json::kind::object: {
      os << "{";
      indent->append(4, ' ');
      auto const& obj = jv.get_object();
      if (!obj.empty()) {
        boost::json::object::const_iterator it = obj.begin();
        for (;;) {
          os << *indent << boost::json::serialize(it->key()) << " : ";
          PrettyPrint(os, it->value(), indent);
          if (++it == obj.end()) {
            break;
          }
          os << ",";
        }
      }
      os << "";
      indent->resize(indent->size() - 4);
      os << *indent << "}";
      break;
    }

    case boost::json::kind::array: {
      os << "[";
      indent->append(4, ' ');
      auto const& arr = jv.get_array();
      if (!arr.empty()) {
        boost::json::array::const_iterator it = arr.begin();
        for (;;) {
          os << *indent;
          PrettyPrint(os, *it, indent);
          if (++it == arr.end()) {
            break;
          }
          os << ",";
        }
      }
      os << "";
      indent->resize(indent->size() - 4);
      os << *indent << "]";
      break;
    }

    case boost::json::kind::string: {
      os << boost::json::serialize(jv.get_string());
      break;
    }

    case boost::json::kind::uint64:
      os << jv.get_uint64();
      break;

    case boost::json::kind::int64:
      os << jv.get_int64();
      break;

    case boost::json::kind::double_:
      os << jv.get_double();
      break;

    case boost::json::kind::bool_:
      if (jv.get_bool()) {
        os << "true";
      } else {
        os << "false";
      }
      break;

    case boost::json::kind::null:
      os << "null";
      break;
  }

  if (indent->empty()) {
    os << "";
  }
}
