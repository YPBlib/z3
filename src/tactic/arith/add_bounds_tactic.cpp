/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    add_bounds_tactic.h

Abstract:

    Tactic for bounding unbounded variables.

Author:

    Leonardo de Moura (leonardo) 2011-10-22.

Revision History:

--*/
#include"tactical.h"
#include"arith_decl_plugin.h"
#include"ast_smt2_pp.h"
#include"bound_manager.h"

struct is_unbounded_proc {
    struct found {};
    arith_util      m_util;
    bound_manager & m_bm;
    
    is_unbounded_proc(bound_manager & bm):m_util(bm.m()), m_bm(bm) {}

    void operator()(app * t) {
        if (is_uninterp_const(t) &&  (m_util.is_int(t) || m_util.is_real(t)) && (!m_bm.has_lower(t) || !m_bm.has_upper(t)))
            throw found();
    }
    
    void operator()(var *) {}
    
    void operator()(quantifier*) {}
};

bool is_unbounded(goal const & g) {
    ast_manager & m = g.m();
    bound_manager bm(m);
    bm(g);
    is_unbounded_proc proc(bm);
    return test(g, proc);
}

class is_unbounded_probe : public probe {
public:
    virtual result operator()(goal const & g) {
        return is_unbounded(g);
    }
};

probe * mk_is_unbounded_probe() {
    return alloc(is_unbounded_probe);
}

class add_bounds_tactic : public tactic {
    struct imp {
        ast_manager & m;
        rational      m_lower;
        rational      m_upper;
        volatile bool m_cancel;
        
        imp(ast_manager & _m, params_ref const & p):
            m(_m) {
            updt_params(p);
        }
        
        void updt_params(params_ref const & p) {
            m_lower  = p.get_rat("add_bound_lower", rational(-2));
            m_upper  = p.get_rat("add_bound_upper", rational(2));
        }
        
        void set_cancel(bool f) {
            m_cancel = f;
        }
        
        struct add_bound_proc {
            arith_util       m_util;
            bound_manager &  m_bm;
            goal &           m_goal;
            rational const & m_lower;
            rational const & m_upper;
            unsigned         m_num_bounds;
            
            add_bound_proc(bound_manager & bm, goal & g, rational const & l, rational const & u):
                m_util(bm.m()), 
                m_bm(bm), 
                m_goal(g),
                m_lower(l),
                m_upper(u) {
                m_num_bounds = 0;
            }
            
            void operator()(app * t) {
                if (is_uninterp_const(t) &&  (m_util.is_int(t) || m_util.is_real(t))) {
                    if (!m_bm.has_lower(t)) {
                        m_goal.assert_expr(m_util.mk_le(t, m_util.mk_numeral(m_upper, m_util.is_int(t))));
                        m_num_bounds++;
                    }
                    if (!m_bm.has_upper(t)) {
                        m_goal.assert_expr(m_util.mk_ge(t, m_util.mk_numeral(m_lower, m_util.is_int(t))));
                        m_num_bounds++;
                    }
                }
            }
            
            void operator()(var *) {}
            
            void operator()(quantifier*) {}
        };
        
        virtual void operator()(goal_ref const & g, 
                                goal_ref_buffer & result, 
                                model_converter_ref & mc, 
                                proof_converter_ref & pc,
                                expr_dependency_ref & core) {
            mc = 0; pc = 0; core = 0;
            tactic_report report("add-bounds", *g);
            bound_manager bm(m);
            expr_fast_mark1 visited;
            add_bound_proc proc(bm, *(g.get()), m_lower, m_upper);
            unsigned sz = g->size();
            for (unsigned i = 0; i < sz; i++)
                quick_for_each_expr(proc, visited, g->form(i));
            visited.reset();
            g->inc_depth();
            result.push_back(g.get());
            if (proc.m_num_bounds > 0) 
                g->updt_prec(goal::UNDER);
            report_tactic_progress(":added-bounds", proc.m_num_bounds);
            TRACE("add_bounds", g->display(tout););
        }
    };

    imp *      m_imp;
    params_ref m_params;

public:
    add_bounds_tactic(ast_manager & m, params_ref const & p):
        m_params(p) {
        m_imp = alloc(imp, m, p);
    }

    virtual tactic * translate(ast_manager & m) {
        return alloc(add_bounds_tactic, m, m_params);
    }
    
    virtual ~add_bounds_tactic() {
        dealloc(m_imp);
    }
    
    virtual void updt_params(params_ref const & p) {
        m_params = p;
        m_imp->updt_params(p);
    }

    virtual void collect_param_descrs(param_descrs & r) { 
        r.insert("add_bound_lower", CPK_NUMERAL, "(default: -2) lower bound to be added to unbounded variables.");
        r.insert("add_bound_upper", CPK_NUMERAL, "(default: 2) upper bound to be added to unbounded variables.");
    }
    
    virtual void operator()(goal_ref const & g, 
                            goal_ref_buffer & result, 
                            model_converter_ref & mc, 
                            proof_converter_ref & pc,
                            expr_dependency_ref & core) {
        (*m_imp)(g, result, mc, pc, core);
    }
    
    virtual void cleanup() {
        imp * d = alloc(imp, m_imp->m, m_params);
        #pragma omp critical (tactic_cancel)
        {
            std::swap(d, m_imp);
        }
        dealloc(d);
    }

protected:
    virtual void set_cancel(bool f) {
        if (m_imp)
            m_imp->set_cancel(f);
    }
};

tactic * mk_add_bounds_tactic(ast_manager & m, params_ref const & p) {
    return clean(alloc(add_bounds_tactic, m, p));
}
